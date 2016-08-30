/**
 * This code is licensed under the Boost License, Version 1.0.
 *
 * http://www.boost.org/LICENSE_1_0.txt
 *
 * Boost Software License - Version 1.0 - August 17th, 2003
 *
 * Permission is hereby granted, free of charge, to any person or organization
 * obtaining a copy of the software and accompanying documentation covered by
 * this license (the "Software") to use, reproduce, display, distribute,
 * execute, and transmit the Software, and to prepare derivative works of the
 * Software, and to permit third-parties to whom the Software is furnished to
 * do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including
 * the above license grant, this restriction and the following disclaimer,
 * must be included in all copies of the Software, in whole or in part, and
 * all derivative works of the Software, unless such copies or derivative
 * works are solely in the form of machine-executable object code generated by
 * a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include"lzw.h"
#include<fcntl.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<string.h>
#include <stdint.h>
#include<stdlib.h>
#include<stdio.h>

#define LZW_DEBUG

#ifdef LZW_DEBUG

// #define LZW_DMSG
#define OUT16BIT

#endif

#define LZW_CLD 0x100
#define LZW_EOF 0x101

#define LZW_MAX_BITS 12

#define LZW_MAX_KEY ((1 << LZW_MAX_BITS) - 1) & bitMask[LZW_MAX_BITS]

#define IO_BUF_SIZE 1024

#define toOneWord(a, b) (b & LZW_MAX_KEY) | ((a & 0xff) << LZW_MAX_BITS)

typedef uint8_t lzw_byte;
typedef uint32_t lzw_word;

typedef struct _LzwDictionary {
	lzw_word ptr;
	lzw_word tbl[1 << LZW_MAX_BITS + 1];
} *LzwDictionary;

typedef struct _LzwFile {
	size_t f;
	lzw_byte *buf;
	lzw_byte bbuf;
	size_t pos;
	lzw_byte filledBits;
	size_t len;
	lzw_byte width;
	uint64_t writeBytes;
} *LzwFile;

lzw_word bitMask[] = {
	0,
	0x00001,0x00003,0x00007,0x0000f,
	0x0001f,0x0003f,0x0007f,0x000ff,
	0x001ff,0x003ff,0x007ff,0x00fff,
	0x01fff,0x03fff,0x07fff,0x0ffff,
	0x01fff,0x03fff,0x07fff,0x0ffff,
	0x1ffff,0x3ffff,0x7ffff,0xfffff
};

void lzwClear(LzwDictionary d, LzwFile lis) {
	lis->width = 9;
	d->ptr = LZW_EOF + 1;
}

void lzwInit(LzwDictionary d, LzwFile lis) {
	for( d->ptr = 0 ; d->ptr <= LZW_EOF; d->ptr++) {
		d->tbl[d->ptr] = toOneWord(d->ptr, LZW_MAX_KEY);
	}
	lzwClear(d, lis);
}

lzw_word lzwLookup(LzwDictionary d, lzw_byte csym, lzw_word psym) {
	lzw_word i;
	lzw_word result = LZW_MAX_KEY;
	lzw_word search = toOneWord(csym, psym);
	for(i = (psym==LZW_MAX_KEY || psym==-1 ? csym : psym + 1); i < d->ptr; i++) {
		if(d->tbl[i] == search) {
			result = i;
			break;
		}
	}
#ifdef LZW_DMSG
printf("\tlookup [csym = %x, psym = %x] = %x\n", csym, psym, result);
#endif
	return result;
}

void lzwInstall(LzwDictionary d, LzwFile lis, lzw_byte csym, lzw_word psym) {
#ifdef LZW_DMSG
printf("\tinstall [csym = %x, psym = %x] = %x", csym, psym, d->ptr);
#endif
	d->tbl[d->ptr] = toOneWord(csym, psym);
	if(d->ptr - 1 == bitMask[lis->width] ) {
		lis->width++;
	}
	d->ptr++;
}

lzw_byte lzwInByte(LzwFile inState) {
	lzw_byte result = 0;
	if(inState->pos >= inState->len) {
		inState->writeBytes += inState->pos;
		inState->len = read(inState->f, inState->buf, IO_BUF_SIZE);
		inState->pos = 0;
	}
	if(inState->len > 0) {
		result = inState->buf[inState->pos];
		inState->pos++;
	}
	return result;
}

lzw_word lzwInCode(LzwFile inState) {
	lzw_word result = 0;
#ifdef OUT16BIT
	result |= (lzwInByte(inState) << 8) & 0xff00;
	result |= lzwInByte(inState) & 0xff;
#endif

#ifndef OUT16BIT
	size_t _bits = inState->width;
	size_t bitsToRead;
	lzw_word buf;
	while(_bits>0) {
		if(inState->filledBits == 8) {
			inState->bbuf = lzwInByte(inState);
			inState->filledBits = 0;
		} else {
			bitsToRead = _bits > 8 ? 8 : _bits;
			bitsToRead = bitsToRead > 8 - inState->filledBits ? 8 - inState->filledBits : bitsToRead;
			buf = inState->bbuf & bitMask[8 - inState->filledBits];
			buf = buf >> 8 - inState->filledBits - bitsToRead;
			buf = buf << _bits - bitsToRead;
			result = result | buf;
			inState->filledBits += bitsToRead;
			_bits -= bitsToRead;
		}
	}
#endif

#ifdef LZW_DMSG
printf("read %x, bits=%i\n", result, inState->width);
#endif
	return result;
}

void lzwOutFlush(LzwFile outState) {
	if(outState->pos > 0) {
		outState->writeBytes += write(outState->f, outState->buf, outState->pos);
		outState->pos = 0;
		outState->buf[0] = 0;
		outState->bbuf = 0;
		outState->filledBits = 0;
	}
}

void lzwOutByte(LzwFile outState, lzw_byte outbyte) {
	outState->buf[outState->pos] = outbyte;
	outState->pos++;
#ifdef LZW_DMSG
printf("write %x\n", outbyte);
#endif
	if(outState->pos >= IO_BUF_SIZE) {
		lzwOutFlush(outState);
	}
}

void lzwOutCode(LzwFile outState, lzw_word psym) {
#ifdef LZW_DMSG
printf("write %x, bits=%i\n", psym, outState->width);
#endif

#ifdef OUT16BIT
	lzwOutByte(outState, (psym >> 8)  & 0xff);
	lzwOutByte(outState, psym  & 0xff);
#endif

#ifndef OUT16BIT
	size_t _bits = outState->width;
	lzw_byte buf;
	size_t bitsForOut;
	while(_bits > 0) {
		if(outState->filledBits == 8) {
			lzwOutByte(outState, outState->bbuf);
			outState->filledBits = 0;
			outState->bbuf = 0;
		} else {
			psym &= bitMask[_bits];
			bitsForOut =  _bits > 8 ? 8 : _bits;
			bitsForOut =  bitsForOut > 8 - outState->filledBits ? 8 - outState->filledBits : bitsForOut;
			buf = psym >> (_bits - bitsForOut);
			buf = buf << (8 - outState->filledBits - bitsForOut);
			outState->bbuf |= buf;
			outState->filledBits += bitsForOut;
			_bits -= bitsForOut;
		}
	}
#endif
}

void _lzwCompress(LzwDictionary d, LzwFile inState, LzwFile outState) {
	lzw_word csym = LZW_MAX_KEY;
	lzw_word psym = LZW_MAX_KEY; // lzwInByte(inState);

	inState->len = IO_BUF_SIZE;
	inState->pos = IO_BUF_SIZE;
	while(inState->len > 0) {
		csym = lzwInByte(inState);
		if(inState->len == 0) {
			break;
		}
#ifdef LZW_DMSG
printf("read %x\n",csym);
#endif
		lzw_word i = lzwLookup(d, csym, psym);
		if( i != LZW_MAX_KEY) {
			csym = i;
		} else if(psym != LZW_MAX_KEY) {
			lzwOutCode(outState, psym);
			if(d->ptr >= LZW_MAX_KEY) {
printf("CLD %li\n", ((long)outState->writeBytes+outState->pos));
//				lzwOutCode(outState, csym);
				lzwOutCode(outState, LZW_CLD);
				lzwClear(d, outState);
				csym = LZW_MAX_KEY;
			} else {
				lzwInstall(d, outState, csym, psym);
			}
		}
		psym = csym;
	}
	lzwOutCode(outState, psym);
	lzwOutCode(outState, LZW_EOF);
	if(outState->filledBits != 0) {
		outState->filledBits = 8;
	}
	lzwOutFlush(outState);
}

lzw_byte lzwOutString(LzwDictionary d, LzwFile outState, lzw_word csym) {
	lzw_byte result;
	lzw_byte _csym = (d->tbl[csym] >> LZW_MAX_BITS) & 0xff;
	lzw_word _psym = d->tbl[csym] & LZW_MAX_KEY;

	if(_psym != LZW_MAX_KEY) {
		result = lzwOutString(d, outState, _psym);
	} else {
		result = _csym;
	}
	lzwOutByte(outState, _csym);
	return result;
}

void _lzwDecompress(LzwDictionary d, LzwFile inState, LzwFile outState) {
	lzw_word csym = LZW_MAX_KEY, psym  = lzwInCode(inState);
	lzwOutString(d, outState, psym);
	while(1) {
		csym = lzwInCode(inState);
if(csym == LZW_MAX_KEY) {
printf("csym = %x\n", csym);
}
		if(csym == LZW_CLD) {
printf("CLD %li %li\n", ((long)inState->writeBytes+inState->pos-2), ((long)outState->writeBytes+outState->pos));
//	lzwOutString(d, outState, csym);
			lzwClear(d, inState);
			csym = LZW_MAX_KEY;
		} else if(csym == LZW_EOF) {
			break;
		} else {
			if(csym < d->ptr) {
				lzw_byte pchar = lzwOutString(d, outState, csym);
				lzwInstall(d, inState, pchar, psym);
			} else {
				lzw_byte pchar = lzwOutString(d, outState, psym);
				lzwOutByte(outState, pchar);
				lzwInstall(d, inState, pchar, psym);
			}
		}
		psym = csym;
	}
	lzwOutFlush(outState);
}

long lzwCompress(char *src, char *dst) {
	long result = -1;
	struct _LzwFile inState;
	struct _LzwFile outState;

	inState.f = open(src, O_RDONLY);
	if(inState.f >= 0) {
		outState.f = open(dst, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
		if(outState.f >= 0) {
			LzwDictionary dic = calloc(sizeof(struct _LzwDictionary) ,1);

			inState.buf = calloc(IO_BUF_SIZE, 1);
			inState.len = IO_BUF_SIZE+1;
			inState.pos = IO_BUF_SIZE+1;
			inState.filledBits = 8;

			outState.buf = calloc(IO_BUF_SIZE, 1);
			outState.pos = 0;
			outState.bbuf = 0;
			outState.filledBits = 0;
			outState.writeBytes = 0;

			lzwInit(dic, &outState);

			_lzwCompress(dic, &inState, &outState);

			free(dic);
			free(inState.buf);
			free(outState.buf);

			close(outState.f);
		}
		close(inState.f);
	}
	return outState.writeBytes;
}

long lzwDecompress(char *src, char *dst) {
	struct _LzwFile inState;
	struct _LzwFile outState;
	long result = -1;

	inState.f = open(src, O_RDONLY);
	if(inState.f >= 0) {
		outState.f = open(dst, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
		if(outState.f >= 0) {
			LzwDictionary dic = calloc(sizeof(struct _LzwDictionary) ,1);

			inState.buf = calloc(IO_BUF_SIZE, 1);
			inState.pos = 0;
			inState.len = 0;
			inState.filledBits = 8;

			outState.buf = calloc(IO_BUF_SIZE, 1);
			outState.pos = 0;
			outState.writeBytes = 0;

			lzwInit(dic, &inState);

			_lzwDecompress(dic, &inState, &outState);

			free(dic);
			free(inState.buf);
			free(outState.buf);
			close(outState.f);
		}
		close(inState.f);
	}
	return outState.writeBytes;
}

int main(int argc, char *argv[]) {
	if( argc == 4 && strcmp("c", argv[1])==0 ) {
		lzwCompress(argv[2], argv[3]);
	} else if( argc == 4 && strcmp("u", argv[1])==0 ) {
		lzwDecompress(argv[2], argv[3]);
	} else {
		printf("Usage:\n");
		printf("%s c infile outfile\t- to compress infile\n", argv[0]);
		printf("%s u infile outfile\t- to uncompress infile\n", argv[0]);
		exit(1);
	}
	exit(0);
}