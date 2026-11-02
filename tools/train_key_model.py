"""Discriminative key model: softmax over the 24 keys on rotation-invariant chroma features.

Usage: python -I train_key_model.py <features.csv>
For a candidate key (tonic t, mode m) the features are the upper and bass chroma rotated to t
(optionally log-compressed); the score is w_m . features + b_m. Trained by gradient descent on the
cross-entropy over the 24 candidates, scored with 2-fold cross-validation (5 random splits).
Prints the weights trained on all tracks at the end, ready to paste into KeyEngine.h.
"""
import csv, math, random, sys

from learn_profiles import load, rot, grade, WEIGHTS


def features(r, t, use_log):
    up, ba = rot(r["up"], t), rot(r["bass"], t)
    if use_log:
        up = [math.log(x + 0.01) for x in up]
        ba = [math.log(x + 0.01) for x in ba]
    return up + ba + [1.0]


def scores(r, w, use_log):
    out = []
    for t in range(12):
        f = features(r, t, use_log)
        for m in (0, 1):
            out.append(sum(a * b for a, b in zip(w[m], f)))
    return out   # index t * 2 + m


def train(rows, use_log, l2, epochs=150, lr=0.5):
    dim = 25
    w = [[0.0] * dim, [0.0] * dim]
    feats = [[features(r, t, use_log) for t in range(12)] for r in rows]
    for _ in range(epochs):
        grad = [[0.0] * dim, [0.0] * dim]
        for r, fr in zip(rows, feats):
            s = []
            for t in range(12):
                for m in (0, 1):
                    s.append(sum(a * b for a, b in zip(w[m], fr[t])))
            mx = max(s)
            e = [math.exp(x - mx) for x in s]
            z = sum(e)
            truth = r["tonic"] * 2 + (1 if r["minor"] else 0)
            for k in range(24):
                p = e[k] / z - (1.0 if k == truth else 0.0)
                if abs(p) < 1e-6:
                    continue
                t, m = divmod(k, 2)
                fk = fr[t]
                gm = grad[m]
                for i in range(dim):
                    gm[i] += p * fk[i]
        n = len(rows)
        for m in (0, 1):
            for i in range(dim):
                w[m][i] -= lr * (grad[m][i] / n + l2 * w[m][i])
    return w


def evaluate(rows, w, use_log):
    exact = mirex = 0.0
    top = [0, 0, 0]
    for r in rows:
        s = scores(r, w, use_log)
        order = sorted(range(24), key=lambda k: -s[k])
        t, m = divmod(order[0], 2)
        g = grade(r, (t, m == 1))
        exact += g == "exact"
        mirex += WEIGHTS[g]
        pos = order.index(r["tonic"] * 2 + (1 if r["minor"] else 0))
        for k in range(3):
            top[k] += pos <= k
    n = len(rows)
    return exact / n, mirex / n, [x / n for x in top]


def main():
    rows = load(sys.argv[1])
    best = None
    for use_log in (False, True):
        for l2 in (0.0, 0.001, 0.01):
            ex = mx = 0.0
            tops = [0.0, 0.0, 0.0]
            runs = 0
            for split in range(5):
                rnd = random.Random(split)
                idx = list(range(len(rows)))
                rnd.shuffle(idx)
                half = len(idx) // 2
                for tr, te in ((idx[:half], idx[half:]), (idx[half:], idx[:half])):
                    w = train([rows[i] for i in tr], use_log, l2)
                    e, m, t3 = evaluate([rows[i] for i in te], w, use_log)
                    ex += e; mx += m
                    tops = [a + b for a, b in zip(tops, t3)]
                    runs += 1
            ex, mx, tops = ex / runs, mx / runs, [x / runs for x in tops]
            print(f"log {use_log!s:5}  l2 {l2:<6}  exact {ex:.0%}  MIREX {mx:.0%}  top2 {tops[1]:.0%}  top3 {tops[2]:.0%}  (cross-validated)", flush=True)
            if best is None or mx > best[0]:
                best = (mx, use_log, l2)

    _, use_log, l2 = best
    w = train(rows, use_log, l2)
    print(f"\nbest: log {use_log}  l2 {l2}")
    fmt = lambda v: ", ".join(f"{x:.5f}f" for x in v)
    print("major { " + fmt(w[0]) + " }")
    print("minor { " + fmt(w[1]) + " }")


if __name__ == "__main__":
    main()
