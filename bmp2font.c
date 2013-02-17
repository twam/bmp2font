/***************************************************************************
 *   Copyright (C) 2012 by Tobias Müller                                   *
 *   Tobias_Mueller@twam.info                                              *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <inttypes.h>

struct bitmap_t {
	int32_t width;
	int32_t height;
	unsigned int rowwidth;
	uint16_t depth;
	unsigned char *data;
};

int read_bitmap_from_file(const char* filename, bitmap_t* bitmap) {
	int ret = 0;
	FILE *fd;
	char* buffer;
	char header[54];
	unsigned short offset;
	unsigned char bottomup;
	unsigned int align;

	// open file
	if ((fd = fopen(filename, "r")) == NULL) {
		fprintf(stderr, "Error while opening file '%s'.\n", filename);
		ret = -1;
		goto read_bitmap_ret;
	}

	// read header
	if (fgets(header, 54, fd) == NULL) {
		fprintf(stderr, "File '%s' is not a valid Windows Bitmap! (Header too short)\n", filename);
		ret = -2;
		goto read_bitmap_fclose;
	}

	// check signature
	if ((header[0] != 0x42) || (header[1] != 0x4D)) {
		fprintf(stderr, "File '%s' is not a valid Windows Bitmap! (Wrong signature: 0x%X%X)\n", filename, header[0], header[1]);
		ret = -3;
		goto read_bitmap_fclose;
	}

	// offset where image data start
	offset = (header[11] << 8) | header[10];

	// read width from header, should be positiv
	bitmap->width = *(int32_t*)(header+18);

	// read height from header
	bitmap->height = *(int32_t*)(header+22);

	// Is this a bottum up picture?
	bottomup = bitmap->height >= 0;

	// color depth of image, should be 1 for monochromes
	bitmap->depth = *(uint16_t*)(header+28);

	// width of a byte row
	if (bitmap->width % 8) {
		bitmap->rowwidth = ((bitmap->width/8)+1) * bitmap->depth;
	} else {
		bitmap->rowwidth = (bitmap->width/8) * bitmap->depth;
	}

	// 4-byte alignment width of a byte row, align >= bitmap->rowwidth
	if (bitmap->rowwidth % 4) {
		align = ((bitmap->rowwidth / 4)+1)*4;
	} else {
		align = bitmap->rowwidth;
	}

	fprintf(stdout, "File '%s' is a %ix%ix%i bitmap\n", filename, bitmap->width, bitmap->height, bitmap->depth);

	if (bitmap->depth != 1) {
		fprintf(stderr, "File '%s' is not an 1-bit Bitmap!\n", filename);
		ret = -4;
		goto read_bitmap_fclose;
	}

	// jump to offset
	fseek(fd, offset, SEEK_SET);

	if ((bitmap->data = (unsigned char*)malloc(align*bitmap->height)) == NULL) {
		fprintf(stderr, "Could not aquire memory for image data (%u bytes)!\n", align*bitmap->height);
		ret = -5;
		goto read_bitmap_fclose;
	}

	if ((buffer = (char*)malloc(align)) == NULL) {
		fprintf(stderr, "Could not aquire memory for read buffer (%u bytes)!\n", align);
		ret = -6;
		free(bitmap->data);
		goto read_bitmap_fclose;
	}

	for (unsigned int row=0; row<bitmap->height; ++row) {
		fseek(fd, offset+row*align, SEEK_SET);

/**		if (fgets(buffer, align+1, fd) == NULL) {
			printf("Input file ended before all pixels could be read!\n");
			return 7;
		}
**/

		// get char by char
		for (unsigned int col =0; col <= align; ++col) {
			buffer[col] = fgetc(fd);
		}

		if (bottomup) {
			memcpy(bitmap->data+(((bitmap->height-1)-row)*bitmap->rowwidth), buffer, bitmap->rowwidth);
		} else {
			memcpy(bitmap->data+(row*abs(bitmap->width/8)), buffer, bitmap->rowwidth);
		}

	}

	free(buffer);

read_bitmap_fclose:
	fclose(fd);

read_bitmap_ret:
	return ret;
}

int write_font(const char* filename, bitmap_t* bitmap, const char* name, uint8_t font_width, uint8_t font_height) {
	int ret = 0;
	FILE *fd;

	uint8_t font_bytes = font_width/8+1;

		// open file
	if ((fd = fopen(filename, "w")) == NULL) {
		fprintf(stderr, "Error while opening file '%s'.\n", filename);
		ret = -1;
		goto write_font_ret;
	}

	fprintf(fd, "#include \"font.h\"\n");

	fprintf(fd, "uint8_t PROGMEM %s_glyphs[256][%u*%u] = {\n", name, font_bytes, font_height);

	for (unsigned int line = 0; line <= 0xFF; ++line) {
		unsigned int row = line/16;
		unsigned int col = line%16;

		fprintf(fd, "\t{");

		for (unsigned int y = 0; y < font_height; ++y) {

			for (unsigned int byte = 0; byte < font_bytes; ++byte) {
				unsigned int data = 0;

				for (unsigned int bit = 0; bit < ((byte < font_bytes - 1) ? 8 : font_width % 8); ++bit) {
					// pixel number
					unsigned int pixel = 16*font_width*font_height*row+16*font_width*y+col*font_width+(byte*8+bit);

					// byte in data
					unsigned int data_byte = (pixel)/8;
					unsigned int data_bit = pixel%8;

//					if (byte == 1) {
					if (bitmap->data[data_byte] & (1<<(7-data_bit))) {
						data |= (1<<bit);
					}
				}

				fprintf(fd, "0x%02X", data);

				if (!((y == font_height-1) && (byte == font_bytes-1))) {
					fprintf(fd, ", ");
				}
			}

		}

		if (line == 0xFF) {
			fprintf(fd, "}\n");
		} else {
			fprintf(fd, "},\n");
		}
	}
	fprintf(fd, "\t};\n");

	fprintf(fd, "font_t PROGMEM %s = {\n", name);
	fprintf(fd, "\t%u,\n", font_width);
	fprintf(fd, "\t%u,\n", font_height);
	fprintf(fd, "\t%s_glyphs\n", name);
	fprintf(fd, "\t};\n");

write_font_fclose:
	fclose(fd);

write_font_ret:
	return ret;
}

int main(int argc, char* argv[]) {
	bitmap_t bitmap;

	if (argc != 4) {
		printf("You need to call: %s <inputfile> <outputfile> <fontname> \n",argv[0]);
		printf("  e.g. %s input.bmp image.h image\n",argv[0]);
		return 1;
	}

	// read bitmap
	if (read_bitmap_from_file(argv[1], &bitmap)<0) {
		fprintf(stderr, "Error while opening file '%s'!\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	uint8_t font_width = bitmap.width / 16;
	uint8_t font_height = bitmap.height / 16;

	if ((bitmap.width % 16) || (bitmap.height % 16)) {
		fprintf(stderr, "Could not detect font size!\n");
		exit(EXIT_FAILURE);
	}

	printf("Font width is %u pixels.\n", font_width);
	printf("Font height is %u pixels.\n", font_height);

	write_font(argv[2], &bitmap, argv[3], font_width, font_height);

	// clean up bitmap
	free(bitmap.data);

	return 0;
}
