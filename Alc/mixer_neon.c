#include "config.h"

#include <arm_neon.h>

#include "AL/al.h"
#include "AL/alc.h"
#include "alMain.h"
#include "alu.h"
#include "hrtf.h"
#include "mixer_defs.h"


const ALfloat *Resample_lerp32_Neon(const BsincState* UNUSED(state), const ALfloat *restrict src,
                                    ALuint frac, ALint increment, ALfloat *restrict dst,
                                    ALsizei numsamples)
{
    const int32x4_t increment4 = vdupq_n_s32(increment*4);
    const float32x4_t fracOne4 = vdupq_n_f32(1.0f/FRACTIONONE);
    const uint32x4_t fracMask4 = vdupq_n_u32(FRACTIONMASK);
    alignas(16) ALint pos_[4];
    alignas(16) ALuint frac_[4];
    int32x4_t pos4;
    uint32x4_t frac4;
    ALsizei i;

    InitiatePositionArrays(frac, increment, frac_, pos_, 4);

    frac4 = vld1q_u32(frac_);
    pos4 = vld1q_s32(pos_);

    for(i = 0;numsamples-i > 3;i += 4)
    {
        const float32x4_t val1 = (float32x4_t){src[pos_[0]], src[pos_[1]], src[pos_[2]], src[pos_[3]]};
        const float32x4_t val2 = (float32x4_t){src[pos_[0]+1], src[pos_[1]+1], src[pos_[2]+1], src[pos_[3]+1]};

        /* val1 + (val2-val1)*mu */
        const float32x4_t r0 = vsubq_f32(val2, val1);
        const float32x4_t mu = vmulq_f32(vcvtq_f32_u32(frac4), fracOne4);
        const float32x4_t out = vmlaq_f32(val1, mu, r0);

        vst1q_f32(&dst[i], out);

        frac4 = vaddq_u32(frac4, (uint32x4_t)increment4);
        pos4 = vaddq_s32(pos4, (int32x4_t)vshrq_n_u32(frac4, FRACTIONBITS));
        frac4 = vandq_u32(frac4, fracMask4);

        vst1q_s32(pos_, pos4);
    }

    if(i < numsamples)
    {
        /* NOTE: These four elements represent the position *after* the last
         * four samples, so the lowest element is the next position to
         * resample.
         */
        ALint pos = pos_[0];
        frac = vgetq_lane_u32(frac4, 0);
        do {
            dst[i] = lerp(src[pos], src[pos+1], frac * (1.0f/FRACTIONONE));

            frac += increment;
            pos  += frac>>FRACTIONBITS;
            frac &= FRACTIONMASK;
        } while(++i < numsamples);
    }
    return dst;
}

const ALfloat *Resample_fir4_32_Neon(const BsincState* UNUSED(state), const ALfloat *restrict src,
                                     ALuint frac, ALint increment, ALfloat *restrict dst,
                                     ALsizei numsamples)
{
    const int32x4_t increment4 = vdupq_n_s32(increment*4);
    const uint32x4_t fracMask4 = vdupq_n_u32(FRACTIONMASK);
    alignas(16) ALint pos_[4];
    alignas(16) ALuint frac_[4];
    int32x4_t pos4;
    uint32x4_t frac4;
    ALsizei i;

    InitiatePositionArrays(frac, increment, frac_, pos_, 4);

    frac4 = vld1q_u32(frac_);
    pos4 = vld1q_s32(pos_);

    --src;
    for(i = 0;numsamples-i > 3;i += 4)
    {
        const float32x4_t val0 = vld1q_f32(&src[pos_[0]]);
        const float32x4_t val1 = vld1q_f32(&src[pos_[1]]);
        const float32x4_t val2 = vld1q_f32(&src[pos_[2]]);
        const float32x4_t val3 = vld1q_f32(&src[pos_[3]]);
        float32x4_t k0 = vld1q_f32(ResampleCoeffs.FIR4[frac_[0]]);
        float32x4_t k1 = vld1q_f32(ResampleCoeffs.FIR4[frac_[1]]);
        float32x4_t k2 = vld1q_f32(ResampleCoeffs.FIR4[frac_[2]]);
        float32x4_t k3 = vld1q_f32(ResampleCoeffs.FIR4[frac_[3]]);
        float32x4_t out;

        k0 = vmulq_f32(k0, val0);
        k1 = vmulq_f32(k1, val1);
        k2 = vmulq_f32(k2, val2);
        k3 = vmulq_f32(k3, val3);
        k0 = vcombine_f32(vpadd_f32(vget_low_f32(k0), vget_high_f32(k0)),
                          vpadd_f32(vget_low_f32(k1), vget_high_f32(k1)));
        k2 = vcombine_f32(vpadd_f32(vget_low_f32(k2), vget_high_f32(k2)),
                          vpadd_f32(vget_low_f32(k3), vget_high_f32(k3)));
        out = vcombine_f32(vpadd_f32(vget_low_f32(k0), vget_high_f32(k0)),
                           vpadd_f32(vget_low_f32(k2), vget_high_f32(k2)));

        vst1q_f32(&dst[i], out);

        frac4 = vaddq_u32(frac4, (uint32x4_t)increment4);
        pos4 = vaddq_s32(pos4, (int32x4_t)vshrq_n_u32(frac4, FRACTIONBITS));
        frac4 = vandq_u32(frac4, fracMask4);

        vst1q_s32(pos_, pos4);
        vst1q_u32(frac_, frac4);
    }

    if(i < numsamples)
    {
        /* NOTE: These four elements represent the position *after* the last
         * four samples, so the lowest element is the next position to
         * resample.
         */
        ALint pos = pos_[0];
        frac = frac_[0];
        do {
            dst[i] = resample_fir4(src[pos], src[pos+1], src[pos+2], src[pos+3], frac);

            frac += increment;
            pos  += frac>>FRACTIONBITS;
            frac &= FRACTIONMASK;
        } while(++i < numsamples);
    }
    return dst;
}

const ALfloat *Resample_fir8_32_Neon(const BsincState* UNUSED(state), const ALfloat *restrict src,
                                     ALuint frac, ALint increment, ALfloat *restrict dst,
                                     ALsizei numsamples)
{
    const int32x4_t increment4 = vdupq_n_s32(increment*4);
    const uint32x4_t fracMask4 = vdupq_n_u32(FRACTIONMASK);
    alignas(16) ALint pos_[4];
    alignas(16) ALuint frac_[4];
    int32x4_t pos4;
    uint32x4_t frac4;
    ALsizei i, j;

    InitiatePositionArrays(frac, increment, frac_, pos_, 4);

    frac4 = vld1q_u32(frac_);
    pos4 = vld1q_s32(pos_);

    src -= 3;
    for(i = 0;numsamples-i > 3;i += 4)
    {
        float32x4_t out[2];
        for(j = 0;j < 8;j+=4)
        {
            const float32x4_t val0 = vld1q_f32(&src[pos_[0]+j]);
            const float32x4_t val1 = vld1q_f32(&src[pos_[1]+j]);
            const float32x4_t val2 = vld1q_f32(&src[pos_[2]+j]);
            const float32x4_t val3 = vld1q_f32(&src[pos_[3]+j]);
            float32x4_t k0 = vld1q_f32(&ResampleCoeffs.FIR4[frac_[0]][j]);
            float32x4_t k1 = vld1q_f32(&ResampleCoeffs.FIR4[frac_[1]][j]);
            float32x4_t k2 = vld1q_f32(&ResampleCoeffs.FIR4[frac_[2]][j]);
            float32x4_t k3 = vld1q_f32(&ResampleCoeffs.FIR4[frac_[3]][j]);

            k0 = vmulq_f32(k0, val0);
            k1 = vmulq_f32(k1, val1);
            k2 = vmulq_f32(k2, val2);
            k3 = vmulq_f32(k3, val3);
            k0 = vcombine_f32(vpadd_f32(vget_low_f32(k0), vget_high_f32(k0)),
                              vpadd_f32(vget_low_f32(k1), vget_high_f32(k1)));
            k2 = vcombine_f32(vpadd_f32(vget_low_f32(k2), vget_high_f32(k2)),
                              vpadd_f32(vget_low_f32(k3), vget_high_f32(k3)));
            out[j>>2] = vcombine_f32(vpadd_f32(vget_low_f32(k0), vget_high_f32(k0)),
                                     vpadd_f32(vget_low_f32(k2), vget_high_f32(k2)));
        }

        out[0] = vaddq_f32(out[0], out[1]);
        vst1q_f32(&dst[i], out[0]);

        frac4 = vaddq_u32(frac4, (uint32x4_t)increment4);
        pos4 = vaddq_s32(pos4, (int32x4_t)vshrq_n_u32(frac4, FRACTIONBITS));
        frac4 = vandq_u32(frac4, fracMask4);

        vst1q_s32(pos_, pos4);
        vst1q_u32(frac_, frac4);
    }

    if(i < numsamples)
    {
        /* NOTE: These four elements represent the position *after* the last
         * four samples, so the lowest element is the next position to
         * resample.
         */
        ALint pos = pos_[0];
        frac = frac_[0];
        do {
            dst[i] = resample_fir8(src[pos  ], src[pos+1], src[pos+2], src[pos+3],
                                   src[pos+4], src[pos+5], src[pos+6], src[pos+7], frac);

            frac += increment;
            pos  += frac>>FRACTIONBITS;
            frac &= FRACTIONMASK;
        } while(++i < numsamples);
    }
    return dst;
}

const ALfloat *Resample_bsinc32_Neon(const BsincState *state, const ALfloat *restrict src,
                                     ALuint frac, ALint increment, ALfloat *restrict dst,
                                     ALsizei dstlen)
{
    const float32x4_t sf4 = vdupq_n_f32(state->sf);
    const ALsizei m = state->m;
    const ALfloat *fil, *scd, *phd, *spd;
    ALsizei pi, i, j;
    float32x4_t r4;
    ALfloat pf;

    src += state->l;
    for(i = 0;i < dstlen;i++)
    {
        // Calculate the phase index and factor.
#define FRAC_PHASE_BITDIFF (FRACTIONBITS-BSINC_PHASE_BITS)
        pi = frac >> FRAC_PHASE_BITDIFF;
        pf = (frac & ((1<<FRAC_PHASE_BITDIFF)-1)) * (1.0f/(1<<FRAC_PHASE_BITDIFF));
#undef FRAC_PHASE_BITDIFF

        fil = ASSUME_ALIGNED(state->coeffs[pi].filter, 16);
        scd = ASSUME_ALIGNED(state->coeffs[pi].scDelta, 16);
        phd = ASSUME_ALIGNED(state->coeffs[pi].phDelta, 16);
        spd = ASSUME_ALIGNED(state->coeffs[pi].spDelta, 16);

        // Apply the scale and phase interpolated filter.
        r4 = vdupq_n_f32(0.0f);
        {
            const float32x4_t pf4 = vdupq_n_f32(pf);
            for(j = 0;j < m;j+=4)
            {
                /* f = ((fil + sf*scd) + pf*(phd + sf*spd)) */
                const float32x4_t f4 = vmlaq_f32(vmlaq_f32(vld1q_f32(&fil[j]),
                                                           sf4, vld1q_f32(&scd[j])),
                    pf4, vmlaq_f32(vld1q_f32(&phd[j]),
                        sf4, vld1q_f32(&spd[j])
                    )
                );
                /* r += f*src */
                r4 = vmlaq_f32(r4, f4, vld1q_f32(&src[j]));
            }
        }
        r4 = vaddq_f32(r4, vcombine_f32(vrev64_f32(vget_high_f32(r4)),
                                        vrev64_f32(vget_low_f32(r4))));
        dst[i] = vget_lane_f32(vadd_f32(vget_low_f32(r4), vget_high_f32(r4)), 0);

        frac += increment;
        src  += frac>>FRACTIONBITS;
        frac &= FRACTIONMASK;
    }
    return dst;
}


static inline void ApplyCoeffsStep(ALsizei Offset, ALfloat (*restrict Values)[2],
                                   const ALsizei IrSize,
                                   ALfloat (*restrict Coeffs)[2],
                                   const ALfloat (*restrict CoeffStep)[2],
                                   ALfloat left, ALfloat right)
{
    ALsizei c;
    float32x4_t leftright4;
    {
        float32x2_t leftright2 = vdup_n_f32(0.0);
        leftright2 = vset_lane_f32(left, leftright2, 0);
        leftright2 = vset_lane_f32(right, leftright2, 1);
        leftright4 = vcombine_f32(leftright2, leftright2);
    }
    Values = ASSUME_ALIGNED(Values, 16);
    Coeffs = ASSUME_ALIGNED(Coeffs, 16);
    CoeffStep = ASSUME_ALIGNED(CoeffStep, 16);
    for(c = 0;c < IrSize;c += 2)
    {
        const ALsizei o0 = (Offset+c)&HRIR_MASK;
        const ALsizei o1 = (o0+1)&HRIR_MASK;
        float32x4_t vals = vcombine_f32(vld1_f32((float32_t*)&Values[o0][0]),
                                        vld1_f32((float32_t*)&Values[o1][0]));
        float32x4_t coefs = vld1q_f32((float32_t*)&Coeffs[c][0]);
        float32x4_t deltas = vld1q_f32(&CoeffStep[c][0]);

        vals = vmlaq_f32(vals, coefs, leftright4);
        coefs = vaddq_f32(coefs, deltas);

        vst1_f32((float32_t*)&Values[o0][0], vget_low_f32(vals));
        vst1_f32((float32_t*)&Values[o1][0], vget_high_f32(vals));
        vst1q_f32(&Coeffs[c][0], coefs);
    }
}

static inline void ApplyCoeffs(ALsizei Offset, ALfloat (*restrict Values)[2],
                               const ALsizei IrSize,
                               ALfloat (*restrict Coeffs)[2],
                               ALfloat left, ALfloat right)
{
    ALsizei c;
    float32x4_t leftright4;
    {
        float32x2_t leftright2 = vdup_n_f32(0.0);
        leftright2 = vset_lane_f32(left, leftright2, 0);
        leftright2 = vset_lane_f32(right, leftright2, 1);
        leftright4 = vcombine_f32(leftright2, leftright2);
    }
    Values = ASSUME_ALIGNED(Values, 16);
    Coeffs = ASSUME_ALIGNED(Coeffs, 16);
    for(c = 0;c < IrSize;c += 2)
    {
        const ALsizei o0 = (Offset+c)&HRIR_MASK;
        const ALsizei o1 = (o0+1)&HRIR_MASK;
        float32x4_t vals = vcombine_f32(vld1_f32((float32_t*)&Values[o0][0]),
                                        vld1_f32((float32_t*)&Values[o1][0]));
        float32x4_t coefs = vld1q_f32((float32_t*)&Coeffs[c][0]);

        vals = vmlaq_f32(vals, coefs, leftright4);

        vst1_f32((float32_t*)&Values[o0][0], vget_low_f32(vals));
        vst1_f32((float32_t*)&Values[o1][0], vget_high_f32(vals));
    }
}

#define MixHrtf MixHrtf_Neon
#define MixDirectHrtf MixDirectHrtf_Neon
#include "mixer_inc.c"
#undef MixHrtf


void Mix_Neon(const ALfloat *data, ALsizei OutChans, ALfloat (*restrict OutBuffer)[BUFFERSIZE],
              ALfloat *CurrentGains, const ALfloat *TargetGains, ALsizei Counter, ALsizei OutPos,
              ALsizei BufferSize)
{
    ALfloat gain, delta, step;
    float32x4_t gain4;
    ALsizei c;

    data = ASSUME_ALIGNED(data, 16);
    OutBuffer = ASSUME_ALIGNED(OutBuffer, 16);

    delta = (Counter > 0) ? 1.0f/(ALfloat)Counter : 0.0f;

    for(c = 0;c < OutChans;c++)
    {
        ALsizei pos = 0;
        gain = CurrentGains[c];
        step = (TargetGains[c] - gain) * delta;
        if(fabsf(step) > FLT_EPSILON)
        {
            ALsizei minsize = mini(BufferSize, Counter);
            /* Mix with applying gain steps in aligned multiples of 4. */
            if(minsize-pos > 3)
            {
                float32x4_t step4;
                gain4 = vsetq_lane_f32(gain, gain4, 0);
                gain4 = vsetq_lane_f32(gain + step, gain4, 1);
                gain4 = vsetq_lane_f32(gain + step + step, gain4, 2);
                gain4 = vsetq_lane_f32(gain + step + step + step, gain4, 3);
                step4 = vdupq_n_f32(step + step + step + step);
                do {
                    const float32x4_t val4 = vld1q_f32(&data[pos]);
                    float32x4_t dry4 = vld1q_f32(&OutBuffer[c][OutPos+pos]);
                    dry4 = vmlaq_f32(dry4, val4, gain4);
                    gain4 = vaddq_f32(gain4, step4);
                    vst1q_f32(&OutBuffer[c][OutPos+pos], dry4);
                    pos += 4;
                } while(minsize-pos > 3);
                /* NOTE: gain4 now represents the next four gains after the
                 * last four mixed samples, so the lowest element represents
                 * the next gain to apply.
                 */
                gain = vgetq_lane_f32(gain4, 0);
            }
            /* Mix with applying left over gain steps that aren't aligned multiples of 4. */
            for(;pos < minsize;pos++)
            {
                OutBuffer[c][OutPos+pos] += data[pos]*gain;
                gain += step;
            }
            if(pos == Counter)
                gain = TargetGains[c];
            CurrentGains[c] = gain;

            /* Mix until pos is aligned with 4 or the mix is done. */
            minsize = mini(BufferSize, (pos+3)&~3);
            for(;pos < minsize;pos++)
                OutBuffer[c][OutPos+pos] += data[pos]*gain;
        }

        if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))
            continue;
        gain4 = vdupq_n_f32(gain);
        for(;BufferSize-pos > 3;pos += 4)
        {
            const float32x4_t val4 = vld1q_f32(&data[pos]);
            float32x4_t dry4 = vld1q_f32(&OutBuffer[c][OutPos+pos]);
            dry4 = vmlaq_f32(dry4, val4, gain4);
            vst1q_f32(&OutBuffer[c][OutPos+pos], dry4);
        }
        for(;pos < BufferSize;pos++)
            OutBuffer[c][OutPos+pos] += data[pos]*gain;
    }
}

void MixRow_Neon(ALfloat *OutBuffer, const ALfloat *Gains, const ALfloat (*restrict data)[BUFFERSIZE], ALsizei InChans, ALsizei InPos, ALsizei BufferSize)
{
    float32x4_t gain4;
    ALsizei c;

    data = ASSUME_ALIGNED(data, 16);
    OutBuffer = ASSUME_ALIGNED(OutBuffer, 16);

    for(c = 0;c < InChans;c++)
    {
        ALsizei pos = 0;
        ALfloat gain = Gains[c];
        if(!(fabsf(gain) > GAIN_SILENCE_THRESHOLD))
            continue;

        gain4 = vdupq_n_f32(gain);
        for(;BufferSize-pos > 3;pos += 4)
        {
            const float32x4_t val4 = vld1q_f32(&data[c][InPos+pos]);
            float32x4_t dry4 = vld1q_f32(&OutBuffer[pos]);
            dry4 = vmlaq_f32(dry4, val4, gain4);
            vst1q_f32(&OutBuffer[pos], dry4);
        }
        for(;pos < BufferSize;pos++)
            OutBuffer[pos] += data[c][InPos+pos]*gain;
    }
}
