/**
 * @file MelFilter.cpp
 *
 * Triangular filters in Mel frequency scale.
 *
 * This file is part of the Aquila DSP library.
 * Aquila is free software, licensed under the MIT/X11 License. A copy of
 * the license is provided with the library in the LICENSE file.
 *
 * @package Aquila
 * @version 3.0.0-dev
 * @author Zbigniew Siciarz
 * @date 2007-2014
 * @license http://www.opensource.org/licenses/mit-license.php MIT
 * @since 0.3.3
 */

#include "MelFilter.h"
#include <algorithm>
#include <utility>

namespace Aquila
{
    /**
     * Creates the filter and sets sample frequency.
     *
     * @param sampleFrequency sample frequency in Hz
     */
    MelFilter::MelFilter(FrequencyType sampleFrequency):
        m_sampleFrequency(sampleFrequency), m_spectrum()
    {
    }

    /**
     * Move constructor.
     *
     * @param other other filter to be moved from
     */
    MelFilter::MelFilter(MelFilter&& other) noexcept:
        m_sampleFrequency(other.m_sampleFrequency),
        m_spectrum(std::move(other.m_spectrum))
    {
    }

    /**
     * Copy assignment operator.
     *
     * @param other filter to be copied from
     * @return reference to assigned value
     */
    MelFilter& MelFilter::operator=(const MelFilter& other)
    = default;

    /**
     * Designs the Mel filter and creates triangular spectrum.
     *
     * @param filterNum which filter in a sequence it is
     * @param melFilterWidth filter width in Mel scale (eg. 200)
     * @param N filter spectrum size (must be the same as filtered spectrum)
     */
    void MelFilter::createFilter(const std::size_t filterNum,
                                 const FrequencyType melFilterWidth, std::size_t N)
    {
        // calculate frequencies in Mel scale
        const FrequencyType melMinFreq = static_cast<FrequencyType>(filterNum) * melFilterWidth / 2.0;
        const FrequencyType melCenterFreq = melMinFreq + melFilterWidth / 2.0;
        const FrequencyType melMaxFreq = melMinFreq + melFilterWidth;

        // convert frequencies to linear scale
        const FrequencyType minFreq = melToLinear(melMinFreq);
        const FrequencyType centerFreq = melToLinear(melCenterFreq);
        const FrequencyType maxFreq = melToLinear(melMaxFreq);

        // generate filter spectrum in linear scale
        generateFilterSpectrum(minFreq, centerFreq, maxFreq, N);
    }

    /**
     * Returns a single value computed by multiplying signal spectrum with
     * Mel filter spectrum and summing all the products.
     *
     * @param dataSpectrum complex signal spectrum
     * @return dot product of the spectra
     */
    double MelFilter::apply(const SpectrumType& dataSpectrum) const
    {
        double value = 0.0;
        const std::size_t N = dataSpectrum.size();
        for (std::size_t i = 0; i < N; ++i)
        {
            value += std::abs(dataSpectrum[i]) * m_spectrum[i];
        }
        return value;
    }

    /**
     * Generates a vector of values shaped as a triangular filter.
     *
     * ^                       [2]
     * |                        /\
     * |                       /  \
     * |                      /    \
     * |_____________________/      \__________________
     * +--------------------[1]----[3]---------------------> f
     *
     * @param minFreq frequency at [1] in linear scale
     * @param centerFreq frequency at [2] in linear scale
     * @param maxFreq frequency at [3] in linear scale
     * @param N length of the spectrum
     */
    void MelFilter::generateFilterSpectrum(const FrequencyType minFreq,
                                           const FrequencyType centerFreq,
                                           const FrequencyType maxFreq, const std::size_t N)
    {
        m_spectrum.clear();
        m_spectrum.resize(N, 0.0);

        // find spectral peak positions corresponding to frequencies
        const auto minPos = static_cast<std::size_t>(static_cast<FrequencyType>(N) * minFreq / m_sampleFrequency);
        auto maxPos = static_cast<std::size_t>(static_cast<FrequencyType>(N) * maxFreq / m_sampleFrequency);
        // limit maxPos not to write out of bounds of vector storage
        maxPos = std::min(maxPos, N - 1);
        if (maxPos <= minPos) {
            return;
        }

        // outside the triangle spectrum values are 0, guaranteed by
        // earlier call to resize
        for (std::size_t k = minPos; k <= maxPos; ++k)
        {
            constexpr double max = 1.0;
            const FrequencyType currentFreq = (static_cast<FrequencyType>(k) * m_sampleFrequency) / static_cast<FrequencyType>(N);
            if (currentFreq < minFreq)
            {
                continue;
            }
            if (currentFreq < centerFreq)
            {
                // in the triangle on the ascending slope
                m_spectrum[k] = (currentFreq - minFreq) * max / (centerFreq - minFreq);
            }
            else
            {
                if (currentFreq < maxFreq)
                {
                    // in the triangle on the descending slope
                    m_spectrum[k] = (maxFreq - currentFreq) * max / (maxFreq - centerFreq);
                }
            }
        }
    }
}
