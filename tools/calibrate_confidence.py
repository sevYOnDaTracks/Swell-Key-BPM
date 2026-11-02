"""Calibrate the key confidence: map the score gap between the two best keys to the real hit rate.

Usage: python calibrate_confidence.py <features.csv>
Uses the learned profiles (cross-validated), fits p(correct) = 1 / (1 + exp(-(a * gap + b))),
and prints the hit rate per confidence band so the displayed % can be checked.
"""
import math, random, sys

from learn_profiles import load, learn, ranked, corr, rot

BASS_W, MINOR_BIAS = 1.0, 0.06


def key_scores(r, up, ba):
    out = {}
    for t in range(12):
        for mi, minor in enumerate((False, True)):
            s = corr(rot(r["up"], t), up[mi]) + BASS_W * corr(rot(r["bass"], t), ba[mi])
            out[(t, minor)] = s + (MINOR_BIAS if minor else 0.0)
    return out


def main():
    rows = load(sys.argv[1])
    samples = []   # (gap, correct)
    for split in range(10):
        rnd = random.Random(split)
        idx = list(range(len(rows)))
        rnd.shuffle(idx)
        half = len(idx) // 2
        for tr, te in ((idx[:half], idx[half:]), (idx[half:], idx[:half])):
            up, ba = learn([rows[i] for i in tr], "up"), learn([rows[i] for i in tr], "bass")
            for i in te:
                r = rows[i]
                s = key_scores(r, up, ba)
                order = sorted(s, key=lambda k: -s[k])
                samples.append((s[order[0]] - s[order[1]], order[0] == (r["tonic"], r["minor"])))

    # logistic fit by gradient descent
    a, b = 1.0, 0.0
    for _ in range(3000):
        ga = gb = 0.0
        for g, c in samples:
            p = 1.0 / (1.0 + math.exp(-(a * g + b)))
            ga += (p - c) * g
            gb += (p - c)
        a -= 2.0 * ga / len(samples)
        b -= 2.0 * gb / len(samples)
    print(f"fit: a = {a:.3f}, b = {b:.3f}")

    bands = [(0.0, 0.4), (0.4, 0.6), (0.6, 0.75), (0.75, 0.9), (0.9, 1.01)]
    for lo, hi in bands:
        sel = [c for g, c in samples if lo <= 1.0 / (1.0 + math.exp(-(a * g + b))) < hi]
        if sel:
            print(f"shown {lo:.0%}-{min(hi, 1):.0%}: {len(sel) / len(samples):5.1%} of tracks, really right {sum(sel) / len(sel):.0%}")


if __name__ == "__main__":
    main()
