#include "Audio/MilkdropFFT.hpp"
#include "Audio/AudioConstants.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <numeric>

using namespace libprojectM::Audio;

namespace {
constexpr float PI = 3.14159265358979323846f;
constexpr float SAMPLE_RATE = 44100.0f;
} // namespace

class MilkdropFFTTest : public ::testing::Test
{
protected:
    // Standard FFT setup matching production use
    static constexpr size_t SamplesIn = WaveformSamples;   // 480
    static constexpr size_t SamplesOut = SpectrumSamples;  // 512

    MilkdropFFT m_fft{SamplesIn, SamplesOut, false, -1.0f};  // No equalize, no envelope for precise testing
    MilkdropFFT m_fftWithProcessing{SamplesIn, SamplesOut, true, 1.0f};  // With equalize and envelope

    std::vector<float> GenerateSineWave(float frequency, size_t numSamples)
    {
        std::vector<float> samples(numSamples);
        for (size_t i = 0; i < numSamples; ++i)
        {
            samples[i] = std::sin(2.0f * PI * frequency * static_cast<float>(i) / SAMPLE_RATE);
        }
        return samples;
    }

    std::vector<float> GenerateDC(float amplitude, size_t numSamples)
    {
        return std::vector<float>(numSamples, amplitude);
    }

    size_t FindPeakBin(const std::vector<float>& spectrum)
    {
        return static_cast<size_t>(
            std::distance(spectrum.begin(), std::max_element(spectrum.begin(), spectrum.end())));
    }

    float GetTotalEnergy(const std::vector<float>& spectrum)
    {
        return std::accumulate(spectrum.begin(), spectrum.end(), 0.0f);
    }
};

TEST_F(MilkdropFFTTest, ZeroInputProducesZeroOutput)
{
    std::vector<float> zeroInput(SamplesIn, 0.0f);
    std::vector<float> spectrum;

    m_fft.TimeToFrequencyDomain(zeroInput, spectrum);

    ASSERT_EQ(spectrum.size(), SamplesOut);

    float totalEnergy = GetTotalEnergy(spectrum);
    EXPECT_FLOAT_EQ(totalEnergy, 0.0f);
}

TEST_F(MilkdropFFTTest, SineWaveProducesPeakAtCorrectBin)
{
    // Generate a sine wave at 1000 Hz
    float testFrequency = 1000.0f;
    auto sineWave = GenerateSineWave(testFrequency, SamplesIn);
    std::vector<float> spectrum;

    m_fft.TimeToFrequencyDomain(sineWave, spectrum);

    ASSERT_EQ(spectrum.size(), SamplesOut);

    // Calculate expected bin
    // FFT has numFrequencies = SamplesOut * 2 = 1024 points
    // Output is first half = 512 bins
    // Bin resolution = SAMPLE_RATE / numFrequencies = 44100 / 1024 ≈ 43.07 Hz per bin
    // Expected bin for 1000 Hz ≈ 1000 / 43.07 ≈ 23
    size_t numFrequencies = SamplesOut * 2;
    float binResolution = SAMPLE_RATE / static_cast<float>(numFrequencies);
    size_t expectedBin = static_cast<size_t>(std::round(testFrequency / binResolution));

    size_t peakBin = FindPeakBin(spectrum);

    // Allow for some spectral leakage - peak should be within 2 bins of expected
    EXPECT_NEAR(static_cast<double>(peakBin), static_cast<double>(expectedBin), 2.0);

    // Peak should have significant energy
    EXPECT_GT(spectrum[peakBin], 0.0f);
}

TEST_F(MilkdropFFTTest, DifferentFrequenciesProduceDifferentPeaks)
{
    std::vector<float> frequencies = {500.0f, 1000.0f, 2000.0f, 4000.0f};
    size_t numFrequencies = SamplesOut * 2;
    float binResolution = SAMPLE_RATE / static_cast<float>(numFrequencies);

    for (float freq : frequencies)
    {
        auto sineWave = GenerateSineWave(freq, SamplesIn);
        std::vector<float> spectrum;

        m_fft.TimeToFrequencyDomain(sineWave, spectrum);

        size_t expectedBin = static_cast<size_t>(std::round(freq / binResolution));
        size_t peakBin = FindPeakBin(spectrum);

        EXPECT_NEAR(static_cast<double>(peakBin), static_cast<double>(expectedBin), 2.0)
            << "Failed for frequency: " << freq << " Hz";
    }
}

TEST_F(MilkdropFFTTest, DCInputProducesNonZeroOutput)
{
    auto dcSignal = GenerateDC(1.0f, SamplesIn);
    std::vector<float> spectrum;

    m_fft.TimeToFrequencyDomain(dcSignal, spectrum);

    ASSERT_EQ(spectrum.size(), SamplesOut);

    // DC input should produce non-zero output (spectral leakage expected without windowing)
    float totalEnergy = GetTotalEnergy(spectrum);
    EXPECT_GT(totalEnergy, 0.0f);

    // First bin (DC component) should have significant energy
    EXPECT_GT(spectrum[0], 0.0f);
}

TEST_F(MilkdropFFTTest, OutputSizeIsCorrect)
{
    auto sineWave = GenerateSineWave(1000.0f, SamplesIn);
    std::vector<float> spectrum;

    m_fft.TimeToFrequencyDomain(sineWave, spectrum);

    EXPECT_EQ(spectrum.size(), SamplesOut);
    EXPECT_EQ(m_fft.NumFrequencies(), SamplesOut * 2);
}

TEST_F(MilkdropFFTTest, InsufficientInputReturnsEmptyOutput)
{
    std::vector<float> tooShort(SamplesIn / 2, 0.5f);  // Half the required samples
    std::vector<float> spectrum;

    m_fft.TimeToFrequencyDomain(tooShort, spectrum);

    EXPECT_TRUE(spectrum.empty());
}

TEST_F(MilkdropFFTTest, RepeatedCallsProduceConsistentResults)
{
    auto sineWave = GenerateSineWave(1000.0f, SamplesIn);
    std::vector<float> spectrum1, spectrum2;

    m_fft.TimeToFrequencyDomain(sineWave, spectrum1);
    m_fft.TimeToFrequencyDomain(sineWave, spectrum2);

    ASSERT_EQ(spectrum1.size(), spectrum2.size());

    for (size_t i = 0; i < spectrum1.size(); ++i)
    {
        EXPECT_FLOAT_EQ(spectrum1[i], spectrum2[i]) << "Mismatch at bin " << i;
    }
}

TEST_F(MilkdropFFTTest, EnvelopeAndEqualizeAffectOutput)
{
    auto sineWave = GenerateSineWave(1000.0f, SamplesIn);
    std::vector<float> spectrumRaw, spectrumProcessed;

    m_fft.TimeToFrequencyDomain(sineWave, spectrumRaw);
    m_fftWithProcessing.TimeToFrequencyDomain(sineWave, spectrumProcessed);

    ASSERT_EQ(spectrumRaw.size(), spectrumProcessed.size());

    // With envelope and equalization, results should differ
    bool anyDifferent = false;
    for (size_t i = 0; i < spectrumRaw.size(); ++i)
    {
        if (std::abs(spectrumRaw[i] - spectrumProcessed[i]) > 1e-6f)
        {
            anyDifferent = true;
            break;
        }
    }

    EXPECT_TRUE(anyDifferent) << "Envelope/equalization should affect output";
}

TEST_F(MilkdropFFTTest, HighFrequencyNearNyquist)
{
    // Test frequency near Nyquist limit (SAMPLE_RATE / 2 = 22050 Hz)
    // But FFT output only goes to SAMPLE_RATE / 4 = 11025 Hz
    float testFrequency = 10000.0f;
    auto sineWave = GenerateSineWave(testFrequency, SamplesIn);
    std::vector<float> spectrum;

    m_fft.TimeToFrequencyDomain(sineWave, spectrum);

    size_t numFrequencies = SamplesOut * 2;
    float binResolution = SAMPLE_RATE / static_cast<float>(numFrequencies);
    size_t expectedBin = static_cast<size_t>(std::round(testFrequency / binResolution));

    // Should be within valid range
    EXPECT_LT(expectedBin, SamplesOut);

    size_t peakBin = FindPeakBin(spectrum);
    EXPECT_NEAR(static_cast<double>(peakBin), static_cast<double>(expectedBin), 3.0);
}

TEST_F(MilkdropFFTTest, MixedFrequenciesProduceMultiplePeaks)
{
    // Generate a signal with two frequencies
    float freq1 = 500.0f;
    float freq2 = 2000.0f;

    std::vector<float> mixedSignal(SamplesIn);
    for (size_t i = 0; i < SamplesIn; ++i)
    {
        float t = static_cast<float>(i) / SAMPLE_RATE;
        mixedSignal[i] = std::sin(2.0f * PI * freq1 * t) + std::sin(2.0f * PI * freq2 * t);
    }

    std::vector<float> spectrum;
    m_fft.TimeToFrequencyDomain(mixedSignal, spectrum);

    size_t numFrequencies = SamplesOut * 2;
    float binResolution = SAMPLE_RATE / static_cast<float>(numFrequencies);

    size_t expectedBin1 = static_cast<size_t>(std::round(freq1 / binResolution));
    size_t expectedBin2 = static_cast<size_t>(std::round(freq2 / binResolution));

    // Both frequency regions should have significant energy
    // Check that bins near expected frequencies have above-average energy
    float avgEnergy = GetTotalEnergy(spectrum) / static_cast<float>(spectrum.size());

    float energyNearBin1 = spectrum[expectedBin1];
    float energyNearBin2 = spectrum[expectedBin2];

    EXPECT_GT(energyNearBin1, avgEnergy * 2.0f) << "Expected peak near " << freq1 << " Hz";
    EXPECT_GT(energyNearBin2, avgEnergy * 2.0f) << "Expected peak near " << freq2 << " Hz";
}
