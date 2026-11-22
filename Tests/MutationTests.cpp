// JUCE-free tests for the mutation core: determinism, locks, intensity,
// styles, safety, undo/redo, morph, persistence.
#include "Mutation/Mutator.h"
#include "Mutation/UndoStack.h"
#include "DSP/ResonatorBank.h"
#include "DSP/FilterBank.h"
#include "DSP/FormantEngine.h"
#include <cstdio>
#include <functional>

using namespace me;
static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::printf ("  FAIL: %s  [line %d]\n", msg, __LINE__); } } while (0)

static MutationState randomState (uint32_t seed)
{
    Rng r; r.seed (seed); MutationState s; s.seed = seed;
    for (int i = 0; i < kNumSound; ++i) s.n[i] = r.uniform();
    for (int i = 0; i < kNumSound; ++i) s.n[i] = toNorm (i, fromNorm (i, s.n[i]));   // legal values only
    return s;
}
static bool same (const MutationState& a, const MutationState& b) { return std::memcmp (a.n, b.n, sizeof a.n) == 0 && a.seed == b.seed; }
static const Style kStyles[3] = { Style::Organic, Style::Digital, Style::Unstable };

static void test_table()
{
    std::puts ("table: ids unique, defaults legal, normalisation round-trips");
    bool ok = true;
    for (int i = 0; i < kNumParams; ++i)
    {
        for (int j = i + 1; j < kNumParams; ++j) ok &= std::strcmp (kParams[i].id, kParams[j].id) != 0;
        const float d = kParams[i].def;
        ok &= d >= kParams[i].min && d <= kParams[i].max;
        ok &= std::fabs (fromNorm (i, toNorm (i, d)) - d) < 1e-3f * std::fmax (1.0f, std::fabs (d));
        if (kParams[i].kind == Kind::Choice) ok &= numChoices (i) >= 2;
    }
    CHECK (ok, "table consistent");
    CHECK (findParam ("res_feedback") == pid::ResFeed && findParam ("nope") == -1, "findParam");
    CHECK (kNumSound == pid::SpScram + 1 && kNumSound <= 63, "mask fits in 64 bits");
}

static void test_determinism()
{
    std::puts ("mutation: deterministic; seed advances reproducibly");
    for (Style st : kStyles)
    {
        MutateOptions o; o.style = st; o.intensity = 0.8f;
        MutationState base = randomState (11); base.seed = 777;
        auto a = Mutator::mutate (base, o), b = Mutator::mutate (base, o);
        CHECK (same (a, b), "same base + options -> identical result");
        CHECK (a.seed == nextSeed (777), "result carries the next seed");
        base.seed = 778;
        CHECK (! same (a, Mutator::mutate (base, o)), "different seed -> different result");
        auto c1 = Mutator::mutate (a, o), c2 = Mutator::mutate (b, o);
        CHECK (same (c1, c2), "chained mutations reproduce");
    }
}

static void test_locks()
{
    std::puts ("locks: locked parameters never change; locking one does not disturb the rest");
    bool ok = true, groupOk = true, indep = true;
    for (uint32_t seed = 1; seed <= 150; ++seed)
        for (Style st : kStyles)
            for (float inten : { 0.3f, 1.0f })
            {
                const MutationState base = randomState (seed * 7919u);
                Rng r; r.seed (seed * 31u);
                Mask lock = 0; for (int i = 0; i < kNumSound; ++i) if (r.uniform() < 0.4f) lock |= bit (i);
                MutateOptions o; o.style = st; o.intensity = inten; o.locked = lock;
                const auto m = Mutator::mutate (base, o);
                for (int i = 0; i < kNumSound; ++i) if ((lock & bit (i)) && m.n[i] != base.n[i]) ok = false;

                // whole-group lock (resonator) incl. dangerous locked values
                MutateOptions g = o; g.locked = lockMaskFor (0, 1u << 0);
                const auto gm = Mutator::mutate (base, g);
                for (int i = 0; i < kNumSound; ++i) if (kParams[i].group == Group::Resonator && gm.n[i] != base.n[i]) groupOk = false;
            }
    CHECK (ok, "per-parameter locks hold in all styles/intensities");
    CHECK (groupOk, "group lock freezes every resonator parameter");

    for (uint32_t seed = 1; seed <= 100; ++seed)
    {
        MutationState base; base.seed = seed;
        MutateOptions o; o.style = Style::Unstable; o.intensity = 1.0f;
        const auto free = Mutator::mutate (base, o);
        o.locked = bit (pid::Vowel);
        const auto locked = Mutator::mutate (base, o);
        for (int i = 0; i < kNumSound; ++i) if (i != pid::Vowel && free.n[i] != locked.n[i]) indep = false;
        if (locked.n[pid::Vowel] != base.n[pid::Vowel]) indep = false;
    }
    CHECK (indep, "locking vowel leaves every other parameter's result unchanged");
}

static void test_intensity()
{
    std::puts ("intensity: 0 changes nothing; more intensity moves more");
    auto meanDelta = [] (Style st, float inten)
    {
        double sum = 0; int n = 0;
        for (uint32_t seed = 1; seed <= 300; ++seed)
        {
            MutationState base; base.seed = seed; MutateOptions o; o.style = st; o.intensity = inten;
            auto m = Mutator::mutate (base, o);
            for (int i = 0; i < kNumSound; ++i) if (kParams[i].mutates) { sum += std::fabs (m.n[i] - base.n[i]); ++n; }
        }
        return sum / n;
    };
    for (Style st : kStyles)
    {
        CHECK (meanDelta (st, 0.0f) < 1e-6, "intensity 0 leaves sound unchanged");
        const double a = meanDelta (st, 0.15f), b = meanDelta (st, 0.5f), c = meanDelta (st, 1.0f);
        std::printf ("  style %d: mean change %.3f -> %.3f -> %.3f\n", (int) st, a, b, c);
        CHECK (a < b && b < c, "mean change grows with intensity");
    }
}

static void test_styles()
{
    std::puts ("styles: Organic stays musical, Digital quantises, Unstable goes wide");
    bool rootSnap = true, orgFeed = true, digVoices = true, digVowel = true;
    double inh[3] = { 0, 0, 0 }, fb[3] = { 0, 0, 0 };
    const int N = 400;
    for (uint32_t seed = 1; seed <= (uint32_t) N; ++seed)
        for (int si = 0; si < 3; ++si)
        {
            MutationState base; base.seed = seed * 13u;
            MutateOptions o; o.style = kStyles[si]; o.intensity = 1.0f;
            auto m = Mutator::mutate (base, o);
            inh[si] += m.value (pid::ResInharm); fb[si] += m.value (pid::ResFeed);
            if (si < 2) { const float midi = Mutator::hzToMidi (m.value (pid::ResRoot)); rootSnap &= std::fabs (midi - std::round (midi)) < 0.02f; }
            if (si == 0) orgFeed &= m.value (pid::ResFeed) <= 0.41f;
            if (si == 1)
            {
                const int v = (int) m.value (pid::ResVoices); digVoices &= (v == 4 || v == 8 || v == 16 || v == 32);
                const float vw = m.value (pid::Vowel); digVowel &= std::fabs (vw - std::round (vw)) < 0.02f;
            }
        }
    CHECK (rootSnap, "Organic/Digital roots land on exact semitones");
    CHECK (orgFeed, "Organic feedback stays gentle (<= 0.4)");
    CHECK (digVoices, "Digital voice counts are 4/8/16/32");
    CHECK (digVowel, "Digital vowels snap to A E I O U");
    std::printf ("  mean inharmonic: %.2f / %.2f / %.2f   mean feedback: %.2f / %.2f / %.2f\n", inh[0] / N, inh[1] / N, inh[2] / N, fb[0] / N, fb[1] / N, fb[2] / N);
    CHECK (inh[2] / N > inh[0] / N + 0.2, "Unstable is far more inharmonic than Organic");
    CHECK (fb[2] / N > fb[0] / N + 0.2, "Unstable uses far more feedback than Organic");
}

// Run a state through resonator -> formant -> filter with loud noise; return pre-clip peak.
static float renderPeak (const MutationState& s, bool& finite)
{
    const double SR = 48000.0;
    ResonatorBank rb; rb.prepare (SR);
    ResonatorBank::Params p; p.rootHz = s.value (pid::ResRoot); p.voices = (int) s.value (pid::ResVoices);
    p.inharmonic = s.value (pid::ResInharm); p.damping = s.value (pid::ResDamp); p.feedback = s.value (pid::ResFeed); p.spread = s.value (pid::ResSpread);
    rb.setParams (p);
    FilterBank f; f.prepare (SR); f.set ((FilterBank::Type) (int) s.value (pid::FiltType), s.value (pid::FiltCutoff), s.value (pid::FiltRes));
    FormantEngine fe; fe.prepare (SR); fe.set (s.value (pid::Vowel), s.value (pid::FormShift));
    const float fm = s.value (pid::FormMix);
    Rng r; r.seed (99); float peak = 0; finite = true;
    for (int i = 0; i < 24000; ++i)
    {
        const float x = r.bipolar() * 0.9f; float l, rr; rb.process (x, x, l, rr);
        float fl = l, fr = rr; fe.process (fl, fr); l += fm * (fl - l); rr += fm * (fr - rr);
        f.process (l, rr);
        finite &= std::isfinite (l) && std::isfinite (rr);
        peak = std::fmax (peak, std::fmax (std::fabs (l), std::fabs (rr)));
    }
    return peak;
}

static void test_safety()
{
    std::puts ("safety: limits hold over thousands of mutations; output stays bounded");
    bool lim = true, cpu = true, range = true, fin = true; float worst = 0;
    SafetyLimits L; L.maxVoices = 12;
    for (uint32_t seed = 1; seed <= 1200; ++seed)
        for (Style st : kStyles)
        {
            MutationState base = randomState (seed); MutateOptions o; o.style = st; o.intensity = 1.0f; o.limits = L;
            auto m = Mutator::mutate (base, o);
            lim &= m.value (pid::ResFeed) <= L.maxFeedback + 1e-4f;
            lim &= ! (m.value (pid::ResFeed) > L.dampThreshold + 1e-4f && m.value (pid::ResDamp) < L.dampFloor - 1e-4f);
            lim &= m.value (pid::FiltRes) <= L.maxFilterRes + 1e-3f && m.value (pid::SpDrop) <= L.maxDropout + 1e-4f;
            lim &= std::abs ((int) m.value (pid::SpShift)) <= L.maxShift;
            lim &= ! ((int) m.value (pid::FiltType) == 0 && m.value (pid::FiltCutoff) < L.minLowpassHz - 0.5f);
            cpu &= (int) m.value (pid::ResVoices) <= L.maxVoices;
            for (int i = 0; i < kNumSound; ++i) range &= m.n[i] >= 0.0f && m.n[i] <= 1.0f && std::isfinite (m.n[i]);
        }
    CHECK (lim, "feedback/damping/filter/dropout/shift limits respected");
    CHECK (cpu, "voice count honours the CPU budget");
    CHECK (range, "all values finite and in range");

    for (uint32_t seed = 1; seed <= 150; ++seed)
        for (Style st : kStyles)
        {
            MutationState base; base.seed = seed; MutateOptions o; o.style = st; o.intensity = 1.0f;
            bool f; worst = std::fmax (worst, renderPeak (Mutator::mutate (base, o), f)); fin &= f;
        }
    std::printf ("  worst pre-clip peak over 450 mutations: %.3f\n", worst);
    CHECK (fin, "rendered audio finite");
    CHECK (worst < 2.0f, "no dangerous output spikes");
}

static void test_undo()
{
    std::puts ("undo/redo: snapshots, redo invalidation, manual tweaks, capacity");
    UndoStack u (5);
    MutationState a, b, c, out; a.seed = 1; b.seed = 2; c.seed = 3;
    u.commit (a); u.commit (b); u.commit (c);
    CHECK (u.canUndo() && ! u.canRedo(), "after 3 commits: can undo, cannot redo");
    CHECK (u.undo (c, out) && same (out, b), "undo -> b");
    CHECK (u.undo (b, out) && same (out, a), "undo -> a");
    CHECK (! u.undo (a, out), "nothing before a");
    CHECK (u.redo (out) && same (out, b) && u.redo (out) && same (out, c), "redo -> b -> c");
    CHECK (! u.redo (out), "nothing after c");
    u.undo (c, out); MutationState d; d.seed = 4; u.commit (d);
    CHECK (! u.canRedo(), "new commit clears redo");

    UndoStack t; MutationState x, y, tweak; x.seed = 1; y.seed = 2; tweak = y; tweak.n[pid::Vowel] = 0.9f;
    t.commit (x); t.commit (y);
    CHECK (t.undo (tweak, out) && same (out, y), "undo with manual tweaks returns to last snapshot first");
    CHECK (t.undo (y, out) && same (out, x), "then continues back");
    CHECK (t.redo (out) && same (out, y) && t.redo (out) && out.n[pid::Vowel] == 0.9f, "tweak itself is recoverable with redo");

    UndoStack cap (3); for (uint32_t i = 1; i <= 10; ++i) { MutationState s; s.seed = i; cap.commit (s); }
    CHECK (cap.size() == 3, "capacity enforced");
}

static void test_morph()
{
    std::puts ("morph: endpoints, midpoint, locks, safety at every position");
    MutateOptions o; o.style = Style::Unstable; o.intensity = 1.0f;
    MutationState base; base.seed = 5;
    const auto A = Mutator::mutate (base, o); const auto B = Mutator::mutate (A, o);
    const MutationState cur = B;
    const auto m0 = Mutator::morph (A, B, 0.0f, 0, cur), m1 = Mutator::morph (A, B, 1.0f, 0, cur), mh = Mutator::morph (A, B, 0.5f, 0, cur);
    bool e0 = true, e1 = true, mid = true;
    for (int i = 0; i < kNumSound; ++i)
    {
        if (! kParams[i].mutates) { e0 &= m0.n[i] == cur.n[i]; continue; }
        e0 &= std::fabs (m0.n[i] - A.n[i]) < 1e-4f; e1 &= std::fabs (m1.n[i] - B.n[i]) < 1e-4f;
        if (kParams[i].kind == Kind::Float)
        {
            const float lo = std::fmin (A.n[i], B.n[i]) - 1e-3f, hi = std::fmax (A.n[i], B.n[i]) + 1e-3f;
            mid &= mh.n[i] >= lo && mh.n[i] <= hi;   // safety may only pull values inward... check within hull +-eps
        }
    }
    CHECK (e0, "t=0 reproduces A (non-morphing params untouched)");
    CHECK (e1, "t=1 reproduces B");
    CHECK (mid, "t=0.5 lies between A and B");

    const Mask lock = bit (pid::ResRoot) | bit (pid::Vowel);
    bool lk = true, safe = true;
    for (float t = 0.0f; t <= 1.0f; t += 0.05f)
    {
        auto m = Mutator::morph (A, B, t, lock, cur);
        lk &= m.n[pid::ResRoot] == cur.n[pid::ResRoot] && m.n[pid::Vowel] == cur.n[pid::Vowel];
        SafetyLimits L; safe &= m.value (pid::ResFeed) <= L.maxFeedback + 1e-4f && ! (m.value (pid::ResFeed) > L.dampThreshold + 1e-4f && m.value (pid::ResDamp) < L.dampFloor - 1e-4f);
    }
    CHECK (lk, "locked parameters hold their live value while morphing");
    CHECK (safe, "every morph position satisfies the safety limits");
}

static void test_persistence()
{
    std::puts ("persistence: state text round-trips");
    MutationState s = randomState (42); s.seed = 4000000001u;
    MutationState out;
    CHECK (MutationState::fromString (s.toString(), out), "parses");
    bool close = out.seed == s.seed; for (int i = 0; i < kNumSound; ++i) close &= std::fabs (out.n[i] - s.n[i]) < 2e-6f;
    CHECK (close, "values and seed survive");
    CHECK (! MutationState::fromString ("garbage", out) && ! MutationState::fromString ("1,2,3", out), "rejects malformed text");
}

int main()
{
    test_table(); test_determinism(); test_locks(); test_intensity(); test_styles();
    test_safety(); test_undo(); test_morph(); test_persistence();
    std::puts (failures ? "FAILED" : "ALL PASSED");
    return failures ? 1 : 0;
}
