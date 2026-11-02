"""Learn key profiles from exported chroma features, with honest cross-validation.

Usage: python -I learn_profiles.py <features.csv>
Rows: id, tonic, minor, 12 upper chroma, 12 bass chroma, tuning (from SwellKeyBench features).
Profiles are learned on one half of the tracks and scored on the other half (10 random splits).
"""
import csv, math, random, sys

DIATONIC = ([2.0, 0, 1.0, 0, 1.2, 1.0, 0, 1.5, 0, 1.0, 0, 1.0],
            [2.0, 0, 1.0, 1.2, 0, 1.0, 0, 1.5, 1.0, 0, 1.0, 0.3])


def norm(v):
    s = sum(v)
    return [x / s for x in v] if s > 0 else list(v)


def corr(a, b):
    ma, mb = sum(a) / 12, sum(b) / 12
    sab = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    saa = sum((x - ma) ** 2 for x in a)
    sbb = sum((y - mb) ** 2 for y in b)
    return sab / math.sqrt(saa * sbb) if saa > 0 and sbb > 0 else 0.0


def rot(v, t):
    """Chroma rotated so that pitch class t comes first."""
    return [v[(i + t) % 12] for i in range(12)]


def load(path):
    rows = []
    for r in csv.reader(open(path)):
        up, ba = norm([float(x) for x in r[3:15]]), norm([float(x) for x in r[15:27]])
        rows.append({"tonic": int(r[1]), "minor": r[2] == "1", "up": up, "bass": ba})
    return rows


def learn(rows, field):
    """Average chroma of the training tracks, rotated to their tonic, per mode."""
    prof = {False: [0.0] * 12, True: [0.0] * 12}
    for r in rows:
        v = rot(r[field], r["tonic"])
        for i in range(12):
            prof[r["minor"]][i] += v[i]
    return norm(prof[False]), norm(prof[True])


def ranked(r, up_prof, bass_prof, bass_w, minor_bias):
    """All 24 keys, best first."""
    scored = []
    for t in range(12):
        for mi, minor in enumerate((False, True)):
            s = corr(rot(r["up"], t), up_prof[mi])
            if bass_prof is not None and bass_w > 0:
                s += bass_w * corr(rot(r["bass"], t), bass_prof[mi])
            if minor:
                s += minor_bias
            scored.append((s, (t, minor)))
    scored.sort(reverse=True)
    return [k for _, k in scored]


def classify(r, up_prof, bass_prof, bass_w, minor_bias):
    return ranked(r, up_prof, bass_prof, bass_w, minor_bias)[0]


def grade(r, guess):
    t, m = guess
    if t == r["tonic"] and m == r["minor"]:
        return "exact"
    rel = (r["tonic"] + 3) % 12 if r["minor"] else (r["tonic"] + 9) % 12
    if m != r["minor"] and t == rel:
        return "relative"
    if m == r["minor"] and t in ((r["tonic"] + 7) % 12, (r["tonic"] + 5) % 12):
        return "fifth"
    if m != r["minor"] and t == r["tonic"]:
        return "parallel"
    return "other"


WEIGHTS = {"exact": 1.0, "fifth": 0.5, "relative": 0.3, "parallel": 0.2, "other": 0.0}


def score(rows, up_prof, bass_prof, bass_w, minor_bias):
    counts = {k: 0 for k in WEIGHTS}
    for r in rows:
        counts[grade(r, classify(r, up_prof, bass_prof, bass_w, minor_bias))] += 1
    n = len(rows)
    return counts["exact"] / n, sum(WEIGHTS[k] * c for k, c in counts.items()) / n, counts


def main():
    rows = load(sys.argv[1])
    print(f"{len(rows)} tracks, {sum(r['minor'] for r in rows)} minor")

    base = score(rows, (norm(DIATONIC[0]), norm(DIATONIC[1])), None, 0, 0.05)
    print(f"baseline diatonic (upper only):          exact {base[0]:.0%}  MIREX {base[1]:.0%}")

    results = []
    for bass_w in (0.0, 0.3, 0.6, 1.0):
        for minor_bias in (0.0, 0.03, 0.06):
            ex, mx = [], []
            for split in range(10):
                rnd = random.Random(split)
                idx = list(range(len(rows)))
                rnd.shuffle(idx)
                half = len(idx) // 2
                for train, test in ((idx[:half], idx[half:]), (idx[half:], idx[:half])):
                    tr = [rows[i] for i in train]
                    up, ba = learn(tr, "up"), learn(tr, "bass")
                    e, m, _ = score([rows[i] for i in test], up, ba, bass_w, minor_bias)
                    ex.append(e); mx.append(m)
            results.append((sum(mx) / len(mx), sum(ex) / len(ex), bass_w, minor_bias))
            print(f"learned  bass {bass_w:.1f} minor+ {minor_bias:.2f}   exact {results[-1][1]:.0%}  MIREX {results[-1][0]:.0%}  (cross-validated)")

    best = max(results)
    print(f"\nbest: bass {best[2]} minor+ {best[3]}  exact {best[1]:.0%}  MIREX {best[0]:.0%}")

    # how often the right key is among the first 2 / 3 suggestions (cross-validated)
    hits = [0, 0, 0]
    total = 0
    for split in range(10):
        rnd = random.Random(split)
        idx = list(range(len(rows)))
        rnd.shuffle(idx)
        half = len(idx) // 2
        for train, test in ((idx[:half], idx[half:]), (idx[half:], idx[:half])):
            tr = [rows[i] for i in train]
            up, ba = learn(tr, "up"), learn(tr, "bass")
            for i in test:
                r = rows[i]
                order = ranked(r, up, ba, best[2], best[3])
                pos = order.index((r["tonic"], r["minor"]))
                for k in range(3):
                    hits[k] += 1 if pos <= k else 0
                total += 1
    print(f"right key in top 1: {hits[0] / total:.0%}   top 2: {hits[1] / total:.0%}   top 3: {hits[2] / total:.0%}")
    up, ba = learn(rows, "up"), learn(rows, "bass")
    e, m, counts = score(rows, up, ba, best[2], best[3])
    print("errors with profiles learned on all tracks:", counts)
    fmt = lambda v: ", ".join(f"{x:.4f}f" for x in v)
    print("\nupper major { " + fmt(up[0]) + " }\nupper minor { " + fmt(up[1]) + " }")
    print("bass  major { " + fmt(ba[0]) + " }\nbass  minor { " + fmt(ba[1]) + " }")


if __name__ == "__main__":
    main()
