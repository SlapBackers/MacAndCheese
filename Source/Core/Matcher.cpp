/*
    MatchMaster - C++ port of the Matchering algorithm (GPLv3).
    Each function names the Matchering source it mirrors.
*/
#include "Matcher.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mm
{
namespace
{
    constexpr double pi = 3.14159265358979323846;

    // ---------------------------------------------------------------- FFT helpers
    int orderFor (int size)
    {
        int order = 0;
        while ((1 << order) < size)
            ++order;
        return order;
    }

    // JUCE's inverse real transform scaling differs between backends: calibrate once.
    float inverseScale (juce::dsp::FFT& fft)
    {
        const int n = fft.getSize();
        std::vector<float> buf ((size_t) n * 2, 0.0f);
        buf[0] = 1.0f;
        fft.performRealOnlyForwardTransform (buf.data(), false);
        fft.performRealOnlyInverseTransform (buf.data());
        return buf[0] != 0.0f ? 1.0f / buf[0] : 1.0f;
    }

    // ---------------------------------------------------------------- dsp.py
    struct MidSide
    {
        std::vector<float> mid, side;
    };

    // dsp.lr_to_ms
    MidSide lrToMs (const juce::AudioBuffer<float>& b, float scale)
    {
        const int n = b.getNumSamples();
        const float* l = b.getReadPointer (0);
        const float* r = b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0);
        MidSide ms;
        ms.mid.resize ((size_t) n);
        ms.side.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const float li = l[i] * scale, ri = r[i] * scale;
            const float m = (li + ri) * 0.5f;
            ms.mid[(size_t) i] = m;
            ms.side[(size_t) i] = m - ri;
        }
        return ms;
    }

    double rms (const float* x, int n)
    {
        double acc = 0.0;
        for (int i = 0; i < n; ++i)
            acc += (double) x[i] * (double) x[i];
        return std::sqrt (acc / std::max (1, n));
    }

    double rms (const std::vector<double>& v)
    {
        double acc = 0.0;
        for (auto x : v)
            acc += x * x;
        return std::sqrt (acc / (double) std::max<size_t> (1, v.size()));
    }

    // ---------------------------------------------------------------- match_levels.py
    struct Pieces
    {
        int pieceSize = 0, divisions = 0;
    };

    // __calculate_piece_sizes
    Pieces pieceSizes (int arraySize, const Config& cfg)
    {
        const double maxPiece = cfg.maxPieceSeconds * cfg.sampleRate;
        Pieces p;
        p.divisions = (int) ((double) arraySize / maxPiece) + 1;
        p.pieceSize = (int) ((double) arraySize / p.divisions);
        return p;
    }

    // get_average_rms -> (rmses, average_rms)
    std::vector<double> pieceRmses (const std::vector<float>& sig, const Pieces& p)
    {
        std::vector<double> r ((size_t) p.divisions);
        for (int d = 0; d < p.divisions; ++d)
            r[(size_t) d] = rms (sig.data() + (size_t) d * (size_t) p.pieceSize, p.pieceSize);
        return r;
    }

    // get_lpis_and_match_rms
    double loudestPieces (const std::vector<double>& rmses, std::vector<int>& idx)
    {
        const double avg = rms (rmses);
        idx.clear();
        double acc = 0.0;
        for (size_t i = 0; i < rmses.size(); ++i)
        {
            if (rmses[i] >= avg)
            {
                idx.push_back ((int) i);
                acc += rmses[i] * rmses[i];
            }
        }
        if (idx.empty()) // numerical safety; Matchering always has >= 1
        {
            for (size_t i = 0; i < rmses.size(); ++i)
                idx.push_back ((int) i);
            return avg;
        }
        return std::sqrt (acc / (double) idx.size());
    }

    // ---------------------------------------------------------------- match_frequencies.py
    // __average_fft: scipy.signal.stft(boxcar, nperseg=fft, noverlap=0, boundary=None, padded=False),
    // 'spectrum' scaling (1 / window sum), then |X| averaged over pieces and frames.
    std::vector<double> averageFft (const std::vector<float>& sig, const std::vector<int>& pieces,
                                    int pieceSize, const Config& cfg)
    {
        const int n = cfg.fftSize;
        const int frames = pieceSize / n;
        if (frames < 1)
            throw std::runtime_error ("Audio is too short to analyze.");

        juce::dsp::FFT fft (orderFor (n));
        std::vector<float> buf ((size_t) n * 2);
        std::vector<double> acc ((size_t) cfg.numBins(), 0.0);
        long count = 0;

        for (int p : pieces)
        {
            const float* base = sig.data() + (size_t) p * (size_t) pieceSize;
            for (int f = 0; f < frames; ++f)
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                std::copy (base + (size_t) f * (size_t) n, base + (size_t) (f + 1) * (size_t) n, buf.begin());
                fft.performRealOnlyForwardTransform (buf.data(), true);
                for (int k = 0; k < cfg.numBins(); ++k)
                    acc[(size_t) k] += std::hypot ((double) buf[(size_t) (2 * k)], (double) buf[(size_t) (2 * k + 1)]) / (double) n;
                ++count;
            }
        }
        for (auto& v : acc)
            v /= (double) std::max (1L, count);
        return acc;
    }

    // scipy.interpolate.interp1d(kind="cubic") == not-a-knot cubic spline.
    class CubicSpline
    {
    public:
        CubicSpline (std::vector<double> xs, std::vector<double> ys) : x (std::move (xs)), y (std::move (ys))
        {
            const size_t n = x.size();
            m.assign (n, 0.0);
            if (n < 4)
                return; // falls back to a natural spline (unused for our grids)

            std::vector<double> h (n - 1);
            for (size_t i = 0; i + 1 < n; ++i)
                h[i] = x[i + 1] - x[i];

            const size_t u = n - 2; // unknowns M1..M_{n-2}
            std::vector<double> a (u), b (u), c (u), r (u);
            for (size_t row = 0; row < u; ++row)
            {
                const size_t i = row + 1;
                a[row] = h[i - 1];
                b[row] = 2.0 * (h[i - 1] + h[i]);
                c[row] = h[i];
                r[row] = 6.0 * ((y[i + 1] - y[i]) / h[i] - (y[i] - y[i - 1]) / h[i - 1]);
            }
            // not-a-knot at both ends (M0 and M_{n-1} eliminated)
            b[0] = 2.0 * (h[0] + h[1]) + h[0] * (h[0] + h[1]) / h[1];
            c[0] = h[1] - h[0] * h[0] / h[1];
            a[0] = 0.0;
            const size_t e = u - 1, hn2 = n - 2, hn3 = n - 3;
            a[e] = h[hn3] - h[hn2] * h[hn2] / h[hn3];
            b[e] = 2.0 * (h[hn3] + h[hn2]) + h[hn2] * (h[hn2] + h[hn3]) / h[hn3];
            c[e] = 0.0;
            if (u == 2) // n == 4: both rows are boundary rows
            {
                b[0] = 2.0 * (h[0] + h[1]) + h[0] * (h[0] + h[1]) / h[1];
                c[0] = h[1] - h[0] * h[0] / h[1];
            }

            // Thomas algorithm
            for (size_t i = 1; i < u; ++i)
            {
                const double w = a[i] / b[i - 1];
                b[i] -= w * c[i - 1];
                r[i] -= w * r[i - 1];
            }
            std::vector<double> sol (u);
            sol[u - 1] = r[u - 1] / b[u - 1];
            for (size_t i = u - 1; i-- > 0;)
                sol[i] = (r[i] - c[i] * sol[i + 1]) / b[i];

            for (size_t i = 0; i < u; ++i)
                m[i + 1] = sol[i];
            m[0] = ((h[0] + h[1]) * m[1] - h[0] * m[2]) / h[1];
            m[n - 1] = ((h[hn3] + h[hn2]) * m[n - 2] - h[hn2] * m[n - 3]) / h[hn3];
        }

        // Evaluates (and extrapolates with the end polynomials).
        double operator() (double xq, size_t& hint) const
        {
            const size_t n = x.size();
            size_t i = std::min (hint, n - 2);
            if (xq < x[i] || xq > x[i + 1])
            {
                auto it = std::upper_bound (x.begin(), x.end(), xq);
                i = (size_t) std::clamp<long> ((long) (it - x.begin()) - 1, 0L, (long) n - 2);
            }
            hint = i;
            const double hh = x[i + 1] - x[i];
            const double A = (x[i + 1] - xq) / hh, B = (xq - x[i]) / hh;
            return A * y[i] + B * y[i + 1] + ((A * A * A - A) * m[i] + (B * B * B - B) * m[i + 1]) * hh * hh / 6.0;
        }

    private:
        std::vector<double> x, y, m;
    };
} // namespace

// statsmodels.nonparametric.lowess(it=0) — tricube-weighted local linear regression with delta skipping.
std::vector<double> lowess (const std::vector<double>& x, const std::vector<double>& y, double frac, double delta)
{
    const int n = (int) x.size();
    std::vector<double> fit ((size_t) n, 0.0);
    if (n == 0)
        return fit;

    const int k = std::clamp ((int) (frac * n + 1e-10), 2, n);
    int left = 0, right = k;
    int lastFit = -1;
    int i = 0;
    std::vector<double> w ((size_t) k);

    while (true)
    {
        // update_neighborhood
        while (right < n && x[(size_t) i] > (x[(size_t) left] + x[(size_t) right]) / 2.0)
        {
            ++left;
            ++right;
        }
        const double radius = std::max (x[(size_t) i] - x[(size_t) left], x[(size_t) right - 1] - x[(size_t) i]);

        // calculate_weights
        double sumW = 0.0;
        for (int j = left; j < right; ++j)
        {
            const double d = radius > 0.0 ? std::abs (x[(size_t) j] - x[(size_t) i]) / radius : 0.0;
            double wj = 0.0;
            if (d <= 0.001)
                wj = 1.0;
            else if (d < 0.999)
            {
                const double t = 1.0 - d * d * d;
                wj = t * t * t;
            }
            w[(size_t) (j - left)] = wj;
            sumW += wj;
        }

        // calculate_y_fit
        if (sumW <= 0.0)
            fit[(size_t) i] = y[(size_t) i];
        else
        {
            double xbar = 0.0;
            for (int j = left; j < right; ++j)
                xbar += (w[(size_t) (j - left)] /= sumW) * x[(size_t) j];
            double sqdev = 0.0;
            for (int j = left; j < right; ++j)
                sqdev += w[(size_t) (j - left)] * (x[(size_t) j] - xbar) * (x[(size_t) j] - xbar);

            double yf = 0.0;
            const bool useSlope = std::sqrt (sqdev) > 1e-7 * std::max (radius, 1e-300);
            for (int j = left; j < right; ++j)
            {
                double p = w[(size_t) (j - left)];
                if (useSlope)
                    p *= 1.0 + (x[(size_t) i] - xbar) * (x[(size_t) j] - xbar) / sqdev;
                yf += p * y[(size_t) j];
            }
            fit[(size_t) i] = yf;
        }

        // interpolate_skipped_fits
        if (lastFit >= 0 && i - lastFit > 1)
        {
            const double x0 = x[(size_t) lastFit], x1 = x[(size_t) i];
            for (int j = lastFit + 1; j < i; ++j)
            {
                const double alpha = (x[(size_t) j] - x0) / (x1 - x0);
                fit[(size_t) j] = alpha * fit[(size_t) i] + (1.0 - alpha) * fit[(size_t) lastFit];
            }
        }

        if (i >= n - 1)
            break;

        // update_indices
        lastFit = i;
        const double cut = x[(size_t) lastFit] + delta;
        int kk = lastFit + 1;
        for (; kk < n; ++kk)
        {
            if (x[(size_t) kk] > cut)
                break;
            if (x[(size_t) kk] == x[(size_t) lastFit])
            {
                fit[(size_t) kk] = fit[(size_t) lastFit];
                lastFit = kk;
            }
        }
        if (kk >= n)
            kk = n - 1; // python for-loop leaves k at its last value
        i = std::max (kk - 1, lastFit + 1);
        if (i >= n)
            break;
    }
    return fit;
}

// match_frequencies.__smooth_exponentially
std::vector<float> smoothMatchingCurve (const std::vector<double>& matching, const Config& cfg)
{
    const int bins = cfg.numBins();
    const double nyq = cfg.sampleRate * 0.5;

    std::vector<double> gridLin ((size_t) bins);
    for (int k = 0; k < bins; ++k)
        gridLin[(size_t) k] = nyq * (double) k / (double) (bins - 1);

    const int logPoints = (cfg.fftSize / 2) * cfg.linLogOversampling + 1;
    const double lo = std::log10 (4.0 / (double) cfg.fftSize);
    std::vector<double> gridLog ((size_t) logPoints), xs ((size_t) logPoints);
    for (int j = 0; j < logPoints; ++j)
    {
        const double t = (double) j / (double) (logPoints - 1);
        gridLog[(size_t) j] = nyq * std::pow (10.0, lo + t * (0.0 - lo));
        xs[(size_t) j] = t;
    }
    gridLog.back() = nyq;

    CubicSpline toLog (gridLin, matching);
    std::vector<double> onLog ((size_t) logPoints);
    size_t hint = 0;
    for (int j = 0; j < logPoints; ++j)
        onLog[(size_t) j] = toLog (gridLog[(size_t) j], hint);

    auto filtered = lowess (xs, onLog, cfg.lowessFrac, cfg.lowessDelta);

    CubicSpline toLin (gridLog, filtered);
    std::vector<float> out ((size_t) bins);
    hint = 0;
    for (int k = 0; k < bins; ++k)
        out[(size_t) k] = (float) toLin (gridLin[(size_t) k], hint);
    out[0] = 0.0f;
    out[1] = (float) matching[1];
    return out;
}

ReferenceProfile analyzeReference (const juce::AudioBuffer<float>& buf, const Config& cfg)
{
    if (buf.getNumSamples() < cfg.fftSize * 2)
        throw std::runtime_error ("The reference is too short.");

    ReferenceProfile ref;

    // match_levels.normalize_reference (normalize_clipped = False)
    float peak = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        peak = std::max (peak, buf.getMagnitude (ch, 0, buf.getNumSamples()));
    if (peak <= 0.0f)
        throw std::runtime_error ("The reference is silent.");
    ref.peak = peak;
    double coef = 1.0;
    if (peak < cfg.threshold)
        coef = std::max ((double) cfg.minValue, (double) peak / (double) cfg.threshold);
    ref.finalAmplitudeCoef = coef;

    auto ms = lrToMs (buf, (float) (1.0 / coef));
    const auto p = pieceSizes ((int) ms.mid.size(), cfg);
    std::vector<int> loud;
    ref.matchRms = loudestPieces (pieceRmses (ms.mid, p), loud);
    ref.midSpectrum = averageFft (ms.mid, loud, p.pieceSize, cfg);
    ref.sideSpectrum = averageFft (ms.side, loud, p.pieceSize, cfg);
    return ref;
}

TargetProfile analyzeTarget (const juce::AudioBuffer<float>& buf, const Config& cfg)
{
    if (buf.getNumSamples() < cfg.fftSize * 2)
        throw std::runtime_error ("The captured audio is too short.");

    float peak = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        peak = std::max (peak, buf.getMagnitude (ch, 0, buf.getNumSamples()));
    if (peak <= 0.0f)
        throw std::runtime_error ("The captured audio is silent.");

    TargetProfile t;
    auto ms = lrToMs (buf, 1.0f);
    const auto p = pieceSizes ((int) ms.mid.size(), cfg);
    t.pieceSize = p.pieceSize;
    t.divisions = p.divisions;
    std::vector<int> loud;
    t.matchRms = loudestPieces (pieceRmses (ms.mid, p), loud);
    t.midSpectrum = averageFft (ms.mid, loud, p.pieceSize, cfg);
    t.sideSpectrum = averageFft (ms.side, loud, p.pieceSize, cfg);
    t.mid = std::move (ms.mid);
    return t;
}

MatchResult match (const TargetProfile& tgt, const ReferenceProfile& ref, const Config& cfg)
{
    if (! tgt.isValid() || ! ref.isValid())
        throw std::runtime_error ("Missing analysis.");

    MatchResult res;
    res.curveSampleRate = cfg.sampleRate;
    res.curveFftSize = cfg.fftSize;
    res.finalAmplitudeCoef = ref.finalAmplitudeCoef;

    // stages.__match_levels -> get_rms_c_and_amplify_pair
    res.rmsCoefficient = ref.matchRms / std::max ((double) cfg.minValue, tgt.matchRms);

    // stages.__match_frequencies -> get_fir (target loudest pieces were amplified first)
    auto matchingFor = [&] (const std::vector<double>& t, const std::vector<double>& r)
    {
        std::vector<double> m (r.size());
        for (size_t k = 0; k < r.size(); ++k)
            m[k] = r[k] / std::max ((double) cfg.minValue, t[k] * res.rmsCoefficient);
        return m;
    };
    res.midCurve = smoothMatchingCurve (matchingFor (tgt.midSpectrum, ref.midSpectrum), cfg);
    res.sideCurve = smoothMatchingCurve (matchingFor (tgt.sideSpectrum, ref.sideSpectrum), cfg);

    // stages.__correct_levels: iterate on the clipped, EQ'd mid signal
    auto fir = designFir (res.midCurve, cfg.sampleRate, cfg.fftSize, cfg.sampleRate, cfg.fftSize, 1.0f, 0.0f);
    std::vector<float> mid (tgt.mid);
    for (auto& v : mid)
        v *= (float) res.rmsCoefficient;
    auto resultMid = convolveSame (mid, fir);

    const Pieces p { tgt.pieceSize, tgt.divisions };
    std::vector<float> clipped (resultMid.size());
    std::vector<int> loud;
    for (int step = 0; step < cfg.rmsCorrectionSteps; ++step)
    {
        for (size_t i = 0; i < resultMid.size(); ++i)
            clipped[i] = std::clamp (resultMid[i], -1.0f, 1.0f);
        const double m = loudestPieces (pieceRmses (clipped, p), loud);
        const double c = ref.matchRms / std::max ((double) cfg.minValue, m);
        for (auto& v : resultMid)
            v *= (float) c;
        res.correctionGain *= c;
    }
    return res;
}

std::vector<float> designFir (const std::vector<float>& curve, double curveRate, int curveFft,
                              double firRate, int n, float amount, float maxBoostDb)
{
    juce::dsp::FFT fft (orderFor (n));
    std::vector<float> buf ((size_t) n * 2, 0.0f);
    const double binHz = curveRate / (double) curveFft;
    const int last = (int) curve.size() - 1;
    const float maxLin = maxBoostDb > 0.0f ? juce::Decibels::decibelsToGain (maxBoostDb) : 0.0f;

    auto magAt = [&] (int k) -> float
    {
        if (amount <= 0.0f)
            return 1.0f;
        const double pos = (double) k * firRate / (double) n / binHz;
        float v;
        if (pos >= last)
            v = curve[(size_t) last];
        else
        {
            const int i0 = (int) pos;
            const float frac = (float) (pos - i0);
            v = curve[(size_t) i0] + frac * (curve[(size_t) i0 + 1] - curve[(size_t) i0]);
        }
        if (amount != 1.0f)
            v = std::copysign (std::pow (std::abs (v), amount), v);
        if (maxLin > 0.0f)
            v = std::copysign (std::clamp (std::abs (v), 1.0f / maxLin, maxLin), v);
        return v;
    };

    for (int k = 0; k <= n / 2; ++k)
    {
        const float v = magAt (k);
        buf[(size_t) (2 * k)] = v;
        if (k > 0 && k < n / 2)
            buf[(size_t) (2 * (n - k))] = v;
    }
    if (amount > 0.0f && maxLin > 0.0f)
        buf[0] = 0.0f; // Matchering removes DC; keep that even when clamped

    fft.performRealOnlyInverseTransform (buf.data());
    const float scale = inverseScale (fft);

    // np.fft.ifftshift + scipy.signal.windows.hann (symmetric)
    std::vector<float> fir ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const float s = buf[(size_t) ((i + n / 2) % n)] * scale;
        const double w = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) (n - 1));
        fir[(size_t) i] = (float) (s * w);
    }
    return fir;
}

int firSizeForRate (double sampleRate)
{
    const int wanted = (int) std::ceil (4096.0 * sampleRate / 44100.0 - 1e-6);
    return 1 << orderFor (std::max (wanted, 4096));
}

std::vector<float> convolveSame (const std::vector<float>& x, const std::vector<float>& h)
{
    const int lx = (int) x.size(), lh = (int) h.size();
    const int nfft = 1 << orderFor (2 * lh);
    const int block = nfft - lh + 1;
    juce::dsp::FFT fft (orderFor (nfft));
    const float scale = inverseScale (fft);

    std::vector<float> H ((size_t) nfft * 2, 0.0f);
    std::copy (h.begin(), h.end(), H.begin());
    fft.performRealOnlyForwardTransform (H.data(), false);

    const int start = (lh - 1) / 2;
    std::vector<float> full ((size_t) lx + (size_t) lh, 0.0f);
    std::vector<float> X ((size_t) nfft * 2);

    for (int pos = 0; pos < lx; pos += block)
    {
        const int len = std::min (block, lx - pos);
        std::fill (X.begin(), X.end(), 0.0f);
        std::copy (x.begin() + pos, x.begin() + pos + len, X.begin());
        fft.performRealOnlyForwardTransform (X.data(), false);
        for (int k = 0; k < nfft; ++k)
        {
            const float ar = X[(size_t) (2 * k)], ai = X[(size_t) (2 * k + 1)];
            const float br = H[(size_t) (2 * k)], bi = H[(size_t) (2 * k + 1)];
            X[(size_t) (2 * k)] = ar * br - ai * bi;
            X[(size_t) (2 * k + 1)] = ar * bi + ai * br;
        }
        fft.performRealOnlyInverseTransform (X.data());
        const int outLen = std::min (len + lh - 1, (int) full.size() - pos);
        for (int i = 0; i < outLen; ++i)
            full[(size_t) (pos + i)] += X[(size_t) i] * scale;
    }

    return std::vector<float> (full.begin() + start, full.begin() + start + lx);
}

juce::AudioBuffer<float> toStereoAtRate (const juce::AudioBuffer<float>& in, double inRate, double outRate)
{
    const int inCh = std::max (1, in.getNumChannels());
    const int nIn = in.getNumSamples();

    if (std::abs (inRate - outRate) < 0.5)
    {
        juce::AudioBuffer<float> out (2, nIn);
        out.copyFrom (0, 0, in, 0, 0, nIn);
        out.copyFrom (1, 0, in, inCh > 1 ? 1 : 0, 0, nIn);
        return out;
    }

    juce::AudioBuffer<float> src (2, nIn);
    src.copyFrom (0, 0, in, 0, 0, nIn);
    src.copyFrom (1, 0, in, inCh > 1 ? 1 : 0, 0, nIn);

    if (inRate > outRate) // anti-alias before decimating
    {
        auto coeffs = juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod (
            (float) (0.45 * outRate), inRate, 8);
        for (int ch = 0; ch < 2; ++ch)
        {
            for (auto& c : coeffs)
            {
                juce::dsp::IIR::Filter<float> f (c);
                f.reset();
                float* d = src.getWritePointer (ch);
                for (int i = 0; i < nIn; ++i)
                    d[i] = f.processSample (d[i]);
            }
        }
    }

    const double ratio = inRate / outRate;
    const int nOut = std::max (0, (int) ((double) (nIn - 256) / ratio));
    juce::AudioBuffer<float> out (2, nOut);
    for (int ch = 0; ch < 2; ++ch)
    {
        juce::WindowedSincInterpolator interp;
        interp.process (ratio, src.getReadPointer (ch), out.getWritePointer (ch), nOut);
    }
    return out;
}

} // namespace mm
