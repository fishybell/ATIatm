using namespace std;

#include "fasit.h"
#include "common.h"
/*
// disable DEBUG
#ifdef DEBUG
#undef DEBUG
#define DEBUG 0
#endif

// disable TRACE
#ifdef TRACE
#undef TRACE
#define TRACE 0
#endif
*/
int FASIT::messageSeq = 0;

FASIT::FASIT() {
FUNCTION_START("::FASIT()")
   rbuf = NULL;
   rsize = 0;
FUNCTION_END("::FASIT()")
}

FASIT::~FASIT() {
FUNCTION_START("::~FASIT()")
   // free the read buffer
   if (rbuf) {
      delete [] rbuf;
   }
FUNCTION_END("::~FASIT()")
}

void FASIT::addToBuffer(int rsize, char *rbuf) {
FUNCTION_START("::addToBuffer(int rsize, char *rbuf)")
   if (this->rsize > 0) {
      // this->rbuf exists? add new data
      char *tbuf = new char[(this->rsize + rsize)];
      memcpy(tbuf, this->rbuf, this->rsize);
      memcpy(tbuf + (sizeof(char) * this->rsize), rbuf, rsize);
      delete [] this->rbuf;
      this->rbuf = tbuf;
      this->rsize += rsize;
   } else {
      // empty this-> rbuf? copy parameters
      this->rbuf = new char[rsize];
      memcpy(this->rbuf, rbuf, rsize);
      this->rsize = rsize;
   }
FUNCTION_END("::addToBuffer(int rsize, char *rbuf)")
}

// we don't worry about clearing the data before a valid message, just up to the end
void FASIT::clearBuffer(int end) {
FUNCTION_START("::clearBuffer(int end)")
DMSG("clearing rbuf to %i of %i\n", end, rsize)
   if (end >= rsize) {
      // clear the entire buffer
      delete [] rbuf;
      rbuf = NULL;
      rsize = 0;
   } else {
      // clear out everything up to and including end
DMSG("new buffer of %i bytes\n", rsize - end)
      char *tbuf = new char[(rsize - end)];
      memcpy(tbuf, rbuf + (sizeof(char) * end), rsize - end);
      delete [] rbuf;
      rbuf = tbuf;
      rsize -= end;
DMSG("ending with %i left\n", rsize)
   }
FUNCTION_END("::clearBuffer(int end)")
}

// See http://www.ross.net/crc/download/crc_v3.txt for crc howto.
//
// For the most part, think of the crc as the remainder of a division
//   and that by adding the crc to original number and recalculating you
//   get 0 back (no remainder any more)
// It really does polynomial mod 2 math (binary substraction without carries)
//   to allow for only using XOR, but the end result is that it
//   works the same way as standard division, but much quicker
// The crc algorithm we use uses "polynomial mod 2" math. This creates a
//   polynomial out of a binary string using the variable x as an unknown and
//   the bit placement as the exponent. For example "x^3 + x^2 + x^1 + x^0"
//   is to equal 0b1111 and "x^5 + x^2" is equal to 0b10010. The weirdness is that 
//   if we assume each x isn't the same, we can't really add them together
//   and divide the same way as standard math but polynomial mod 2 let's us do
//   binary subtraction and ignoring the carries to compute division. The above
//   link goes into greater detail, but just be assured that the main reason
//   for doing it this way instead of a full division is that it's much quicker
//   as we can just use XOR and fly along the entire stream in a single pass
// If you look at our chosen polynomials they both use the first and last bits
//   and are both 1 bit too long (33 bits and 9 bits). This is normal as
//   apparently you can just assume the most significant bit is set and not
//   actually compute it. I don't really get this part
// There's some serious voodoo magic in choosing a good polynomial as
//   the divisor; I'd explain it here, but first I'd have to understand it
// I don't actually compute the remainder using the given polynomial, but
//   instead use a lookup table as explained in the above link
//
// Usage: Add the computed crc on transmission and check for a computed crc of 0 on receipt

// utility function for crc32 computation
// see section 11 of the above link for an almost explanation of why we are reflecting various bytes
// simple answer: it's what the ethernet crc standard does, and it's a very proven algorithm
__uint32_t reflect(__uint32_t data, int width) {
   __uint32_t reflection = 0;
   int bit = 0;

   do {
      // If the least significant bit is set, set the bit in the opposite location
      if (data & 1) {
         reflection |= (1 << ((width - 1) - bit));
      }

      // move right, throwing away the just reflected bit
      data = (data >> 1);
   } while (bit++ < width);

   return reflection;
}


// using ethernet standard polynomial (x^32 + x^26 + x^23 + x^22 + x^16 + x^12 + x^11 + x^10 + x^8 + x^7 + x^5 + x^4 + x^2 + x^1 + x^0) 
__uint32_t FASIT::crc32(void *buf, int start, int end) {
   static __uint32_t crc32_table[256] = {
      0x00000000, 0x04C11DB7, 0x09823B6E, 0x0D4326D9, 0x130476DC, 0x17C56B6B, 0x1A864DB2, 0x1E475005,
      0x2608EDB8, 0x22C9F00F, 0x2F8AD6D6, 0x2B4BCB61, 0x350C9B64, 0x31CD86D3, 0x3C8EA00A, 0x384FBDBD,
      0x4C11DB70, 0x48D0C6C7, 0x4593E01E, 0x4152FDA9, 0x5F15ADAC, 0x5BD4B01B, 0x569796C2, 0x52568B75,
      0x6A1936C8, 0x6ED82B7F, 0x639B0DA6, 0x675A1011, 0x791D4014, 0x7DDC5DA3, 0x709F7B7A, 0x745E66CD,
      0x9823B6E0, 0x9CE2AB57, 0x91A18D8E, 0x95609039, 0x8B27C03C, 0x8FE6DD8B, 0x82A5FB52, 0x8664E6E5,
      0xBE2B5B58, 0xBAEA46EF, 0xB7A96036, 0xB3687D81, 0xAD2F2D84, 0xA9EE3033, 0xA4AD16EA, 0xA06C0B5D,
      0xD4326D90, 0xD0F37027, 0xDDB056FE, 0xD9714B49, 0xC7361B4C, 0xC3F706FB, 0xCEB42022, 0xCA753D95,
      0xF23A8028, 0xF6FB9D9F, 0xFBB8BB46, 0xFF79A6F1, 0xE13EF6F4, 0xE5FFEB43, 0xE8BCCD9A, 0xEC7DD02D,
      0x34867077, 0x30476DC0, 0x3D044B19, 0x39C556AE, 0x278206AB, 0x23431B1C, 0x2E003DC5, 0x2AC12072,
      0x128E9DCF, 0x164F8078, 0x1B0CA6A1, 0x1FCDBB16, 0x018AEB13, 0x054BF6A4, 0x0808D07D, 0x0CC9CDCA,
      0x7897AB07, 0x7C56B6B0, 0x71159069, 0x75D48DDE, 0x6B93DDDB, 0x6F52C06C, 0x6211E6B5, 0x66D0FB02,
      0x5E9F46BF, 0x5A5E5B08, 0x571D7DD1, 0x53DC6066, 0x4D9B3063, 0x495A2DD4, 0x44190B0D, 0x40D816BA,
      0xACA5C697, 0xA864DB20, 0xA527FDF9, 0xA1E6E04E, 0xBFA1B04B, 0xBB60ADFC, 0xB6238B25, 0xB2E29692,
      0x8AAD2B2F, 0x8E6C3698, 0x832F1041, 0x87EE0DF6, 0x99A95DF3, 0x9D684044, 0x902B669D, 0x94EA7B2A,
      0xE0B41DE7, 0xE4750050, 0xE9362689, 0xEDF73B3E, 0xF3B06B3B, 0xF771768C, 0xFA325055, 0xFEF34DE2,
      0xC6BCF05F, 0xC27DEDE8, 0xCF3ECB31, 0xCBFFD686, 0xD5B88683, 0xD1799B34, 0xDC3ABDED, 0xD8FBA05A,
      0x690CE0EE, 0x6DCDFD59, 0x608EDB80, 0x644FC637, 0x7A089632, 0x7EC98B85, 0x738AAD5C, 0x774BB0EB,
      0x4F040D56, 0x4BC510E1, 0x46863638, 0x42472B8F, 0x5C007B8A, 0x58C1663D, 0x558240E4, 0x51435D53,
      0x251D3B9E, 0x21DC2629, 0x2C9F00F0, 0x285E1D47, 0x36194D42, 0x32D850F5, 0x3F9B762C, 0x3B5A6B9B,
      0x0315D626, 0x07D4CB91, 0x0A97ED48, 0x0E56F0FF, 0x1011A0FA, 0x14D0BD4D, 0x19939B94, 0x1D528623,
      0xF12F560E, 0xF5EE4BB9, 0xF8AD6D60, 0xFC6C70D7, 0xE22B20D2, 0xE6EA3D65, 0xEBA91BBC, 0xEF68060B,
      0xD727BBB6, 0xD3E6A601, 0xDEA580D8, 0xDA649D6F, 0xC423CD6A, 0xC0E2D0DD, 0xCDA1F604, 0xC960EBB3,
      0xBD3E8D7E, 0xB9FF90C9, 0xB4BCB610, 0xB07DABA7, 0xAE3AFBA2, 0xAAFBE615, 0xA7B8C0CC, 0xA379DD7B,
      0x9B3660C6, 0x9FF77D71, 0x92B45BA8, 0x9675461F, 0x8832161A, 0x8CF30BAD, 0x81B02D74, 0x857130C3,
      0x5D8A9099, 0x594B8D2E, 0x5408ABF7, 0x50C9B640, 0x4E8EE645, 0x4A4FFBF2, 0x470CDD2B, 0x43CDC09C,
      0x7B827D21, 0x7F436096, 0x7200464F, 0x76C15BF8, 0x68860BFD, 0x6C47164A, 0x61043093, 0x65C52D24,
      0x119B4BE9, 0x155A565E, 0x18197087, 0x1CD86D30, 0x029F3D35, 0x065E2082, 0x0B1D065B, 0x0FDC1BEC,
      0x3793A651, 0x3352BBE6, 0x3E119D3F, 0x3AD08088, 0x2497D08D, 0x2056CD3A, 0x2D15EBE3, 0x29D4F654,
      0xC5A92679, 0xC1683BCE, 0xCC2B1D17, 0xC8EA00A0, 0xD6AD50A5, 0xD26C4D12, 0xDF2F6BCB, 0xDBEE767C,
      0xE3A1CBC1, 0xE760D676, 0xEA23F0AF, 0xEEE2ED18, 0xF0A5BD1D, 0xF464A0AA, 0xF9278673, 0xFDE69BC4,
      0x89B8FD09, 0x8D79E0BE, 0x803AC667, 0x84FBDBD0, 0x9ABC8BD5, 0x9E7D9662, 0x933EB0BB, 0x97FFAD0C,
      0xAFB010B1, 0xAB710D06, 0xA6322BDF, 0xA2F33668, 0xBCB4666D, 0xB8757BDA, 0xB5365D03, 0xB1F740B4
   };
   char *data = (char*)buf + (sizeof(char) * start);
   int size = end - start;
PRINT_HEXB(data, size)
   __uint32_t crc = 0;

   while (size--) {
      crc = crc32_table[(__uint8_t)(((__uint8_t)reflect(*data, 8)) ^ (crc >> 24))] ^ (crc << 8);
      data++;
   }
DMSG("...has 32 bit crc of 0x%08x\n", reflect(crc, 32))

   return reflect(crc, 32);
}

// based on polynomial x^8 + x^2 + x^1 + x^0
__uint8_t FASIT::crc8(void *buf, int start, int end) {
   static __uint8_t crc8_table[256] = {
      0x00, 0x07, 0x0E, 0x09, 0x1C, 0x1B, 0x12, 0x15, 0x38, 0x3F, 0x36, 0x31, 0x24, 0x23, 0x2A, 0x2D,
      0x70, 0x77, 0x7E, 0x79, 0x6C, 0x6B, 0x62, 0x65, 0x48, 0x4F, 0x46, 0x41, 0x54, 0x53, 0x5A, 0x5D,
      0xE0, 0xE7, 0xEE, 0xE9, 0xFC, 0xFB, 0xF2, 0xF5, 0xD8, 0xDF, 0xD6, 0xD1, 0xC4, 0xC3, 0xCA, 0xCD,
      0x90, 0x97, 0x9E, 0x99, 0x8C, 0x8B, 0x82, 0x85, 0xA8, 0xAF, 0xA6, 0xA1, 0xB4, 0xB3, 0xBA, 0xBD,
      0xC7, 0xC0, 0xC9, 0xCE, 0xDB, 0xDC, 0xD5, 0xD2, 0xFF, 0xF8, 0xF1, 0xF6, 0xE3, 0xE4, 0xED, 0xEA,
      0xB7, 0xB0, 0xB9, 0xBE, 0xAB, 0xAC, 0xA5, 0xA2, 0x8F, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9D, 0x9A,
      0x27, 0x20, 0x29, 0x2E, 0x3B, 0x3C, 0x35, 0x32, 0x1F, 0x18, 0x11, 0x16, 0x03, 0x04, 0x0D, 0x0A,
      0x57, 0x50, 0x59, 0x5E, 0x4B, 0x4C, 0x45, 0x42, 0x6F, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7D, 0x7A,
      0x89, 0x8E, 0x87, 0x80, 0x95, 0x92, 0x9B, 0x9C, 0xB1, 0xB6, 0xBF, 0xB8, 0xAD, 0xAA, 0xA3, 0xA4,
      0xF9, 0xFE, 0xF7, 0xF0, 0xE5, 0xE2, 0xEB, 0xEC, 0xC1, 0xC6, 0xCF, 0xC8, 0xDD, 0xDA, 0xD3, 0xD4,
      0x69, 0x6E, 0x67, 0x60, 0x75, 0x72, 0x7B, 0x7C, 0x51, 0x56, 0x5F, 0x58, 0x4D, 0x4A, 0x43, 0x44,
      0x19, 0x1E, 0x17, 0x10, 0x05, 0x02, 0x0B, 0x0C, 0x21, 0x26, 0x2F, 0x28, 0x3D, 0x3A, 0x33, 0x34,
      0x4E, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5C, 0x5B, 0x76, 0x71, 0x78, 0x7F, 0x6A, 0x6D, 0x64, 0x63,
      0x3E, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2C, 0x2B, 0x06, 0x01, 0x08, 0x0F, 0x1A, 0x1D, 0x14, 0x13,
      0xAE, 0xA9, 0xA0, 0xA7, 0xB2, 0xB5, 0xBC, 0xBB, 0x96, 0x91, 0x98, 0x9F, 0x8A, 0x8D, 0x84, 0x83,
      0xDE, 0xD9, 0xD0, 0xD7, 0xC2, 0xC5, 0xCC, 0xCB, 0xE6, 0xE1, 0xE8, 0xEF, 0xFA, 0xFD, 0xF4, 0xF3
   };
   char *data = (char*)buf + (sizeof(char) * start);
   int size = end - start;
PRINT_HEXB(data, size)
   __uint8_t crc = 0; // initial value of 0

   while (size--) {
      crc = crc8_table[(__uint8_t)(crc ^ *data)];
      data++;
   }
DMSG("...has 8 bit crc of 0x%02x\n", crc)
   return crc;
}

// the parity bit works like a 1 bit crc check
int FASIT::parity(void *buf, int size) {
   // like the crc checks, it's faster to look it up than compute for each bit
   static int parity_table[256] = {
      0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
      1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
      1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
      0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
      1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
      0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
      0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1,
      1, 0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0
   };
   char *data = (char*)buf;
   int par = 0;
DMSG("parity called for %i bytes:\n", size)
PRINT_HEXB(data, size)
   while(size--) {
      par ^= parity_table[(__uint8_t)*data];
      data++;
   }
DMSG("...has parity of %i\n", par)

   return par;
}

// creates a valid ATI header for the given message
// it may need tweaking in certain circumstances
// if any tweaking is done, the parity bit needs to be recalculated
struct ATI_header FASIT::createHeader(int mnum, int source, void *data, int size) {
FUNCTION_START("::createHeader(int mnum, int source, void *data, int size)")
   ATI_header hdr;
   hdr.magic = 7;
   hdr.parity = 0;
   if (size <= 10) {
      hdr.length = 5 + size;
   } else {
      hdr.length = 15;
   }
   hdr.num = mnum;
   hdr.source = source;

   // combine the header parity with the parity of the first 10 bytes of data
   hdr.parity = parity(&hdr, sizeof(ATI_header)) ^ parity(data, min(size, 10));

FUNCTION_HEX("::createHeader(int mnum, int source, void *data, int size)", &hdr)
   return hdr;
}

// swap the byte order to switch between network and host modes
__uint64_t FASIT::swap64(__uint64_t s) {
   __uint64_t r;
   char *a, *b;
   a = (char*) &r;
   b = (char*) &s;

   // swap every byte
   int l = sizeof(__uint64_t) - 1;
   for(int i=0; i<sizeof(__uint64_t); i++) {
      a[i] = b[l--];
   }

FUNCTION_HEX("::swap64(__uint64_t s)", &r)
   return r;
}

