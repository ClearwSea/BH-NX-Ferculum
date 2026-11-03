#ifndef M3_CG_DECODER_H
#define M3_CG_DECODER_H
#include <stdint.h>
#include <stddef.h>
/* open consumes owned bytes on both success and failure. One CG at a time. */
int m3_cg_decoder_open(uint8_t *owned, size_t size);
void m3_cg_decoder_close(void);
int m3_cg_decoder_width(void);
int m3_cg_decoder_height(void);
int64_t m3_cg_decoder_duration(void);
int m3_cg_decoder_has_audio(void);
void m3_cg_decoder_audio(int on, int64_t ms, float volume, float rate);
int m3_cg_decoder_upload(unsigned texture, int64_t ms);
int m3_cg_decoder_probe(unsigned *checksum, unsigned *nonblack);
unsigned m3_cg_decoder_audio_probe(void);
void m3_cg_decoder_probe_texture(unsigned texture);
unsigned m3_cg_decoder_mix_probe(void);
#endif
