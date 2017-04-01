#include "utfconverter.h"

#define calc_sys_diff (((float)afterUsage.ru_stime.tv_sec - (float)beforeUsage.ru_stime.tv_sec) + ((float)afterUsage.ru_stime.tv_usec - (float)beforeUsage.ru_stime.tv_usec) / 1000000)
#define calc_user_diff (((float)afterUsage.ru_utime.tv_sec - (float)beforeUsage.ru_utime.tv_sec) + ((float)afterUsage.ru_utime.tv_usec - (float)beforeUsage.ru_utime.tv_usec) / 1000000)
#define calc_real_diff (((float)timeAfter.tv_sec - (float)timeBefore.tv_sec) + ((float)timeAfter.tv_usec - (float)timeBefore.tv_usec) / 1000000)

char* srcFilename;
char* convFilename;
endianness srcEndian;
endianness convEndian;
encoding srcEncoding;
encoding convEncoding;

int verbosityLevel;
double readingTimes[3];
double convertingTimes[3];
double writingTimes[3];
int glyphCount;
int asciiCount;
int surrogateCount;

int main(int argc, char** argv) { /**  */
	/* After calling parse_args(), filename, convEndian, and convEncoding
	 should be set. */
	struct timeval timeBefore, timeAfter;
	struct rusage beforeUsage, afterUsage;
	int srcFD, convFD;
	unsigned char* buf;
	Glyph* glyph;
	struct stat *srcStats, *convStats; 

 	parse_args(argc, argv); 

	srcFD = open(srcFilename, O_RDONLY);
	if (srcFD == -1) {
		print_help(EXIT_FAILURE);
	}
	if (convFilename != NULL) {
		convFD = open(convFilename, O_RDWR | O_APPEND);
		if (convFD == -1) {
			fclose(fopen(convFilename, "w+"));
			convFD = open(convFilename, O_RDWR | O_APPEND);
		}
	} else {
		convFD = STDOUT_FILENO;
		if (convFD == -1) {
			print_help(EXIT_FAILURE);
		}
	}

	srcStats = malloc(sizeof(struct stat));
	convStats = malloc(sizeof(struct stat));
	if (srcFilename != NULL) {
		if (stat(srcFilename, srcStats) == 0) {
			if (convFilename != NULL) {
				if (stat(convFilename, convStats) == 0) {
					if (srcStats->st_ino == convStats->st_ino) {
						print_help(EXIT_FAILURE);
					}
				}
			}
		}
	}
	free(srcStats);
	free(convStats);

	buf = malloc(sizeof(char));
	*buf = 0;
	glyph = malloc(sizeof(Glyph));

	memset(readingTimes, 0, sizeof(readingTimes));
	memset(convertingTimes, 0, sizeof(convertingTimes));
	memset(writingTimes, 0, sizeof(writingTimes));
	glyphCount = 0;
    asciiCount = 0;
	surrogateCount = 0;

	/** Read BOM */
	getrusage(RUSAGE_SELF, &beforeUsage);
	gettimeofday(&timeBefore, NULL);
	
	if (!read_bom(&srcFD)) {
		free(glyph);
		quit_converter(srcFD, NO_FD, EXIT_FAILURE);
	}

	getrusage(RUSAGE_SELF, &afterUsage);
	readingTimes[SYS] += calc_sys_diff;
	readingTimes[USER] += calc_user_diff;
	gettimeofday(&timeAfter, NULL);
	readingTimes[REAL] += calc_real_diff;

	/** Write BOM to output */
	getrusage(RUSAGE_SELF, &beforeUsage);
	gettimeofday(&timeBefore, NULL);
	
	if (!write_bom(convFD)) {
		free(glyph);
		print_help(EXIT_FAILURE);
	}

	getrusage(RUSAGE_SELF, &afterUsage);
	writingTimes[SYS] += calc_sys_diff;
	writingTimes[USER] += calc_user_diff;
	gettimeofday(&timeAfter, NULL);
	writingTimes[REAL] += calc_real_diff;

	/* Read source file and create glyphs */
	while (read(srcFD, buf, 1) == 1) {
		memset(glyph, 0, sizeof(Glyph));

		/* Read */
		getrusage(RUSAGE_SELF, &beforeUsage);
		gettimeofday(&timeBefore, NULL);

		if (srcEncoding == UTF_8) {
			read_utf_8(srcFD, glyph, buf);
		} else {
			read_utf_16(srcFD, glyph, buf);
 		}

		getrusage(RUSAGE_SELF, &afterUsage);
		readingTimes[SYS] += calc_sys_diff;
		readingTimes[USER] += calc_user_diff;;
		gettimeofday(&timeAfter, NULL);
		readingTimes[REAL] += calc_real_diff;;
		
		/** Convert */
		getrusage(RUSAGE_SELF, &beforeUsage);
		gettimeofday(&timeBefore, NULL);

		if (convEncoding != srcEncoding) {
			convert_encoding(glyph);
		}

		if (convEndian != LITTLE) {
			swap_endianness(glyph);
		}

		getrusage(RUSAGE_SELF, &afterUsage);
		convertingTimes[SYS] += calc_sys_diff;
		convertingTimes[USER] += calc_user_diff;
		gettimeofday(&timeAfter, NULL);
		convertingTimes[REAL] += calc_real_diff;

		/** Write */
		gettimeofday(&timeBefore, NULL);

		write_glyph(glyph, convFD);

		getrusage(RUSAGE_SELF, &afterUsage);
		writingTimes[SYS] += calc_sys_diff;
		writingTimes[USER] += calc_user_diff;
		gettimeofday(&timeAfter, NULL);
		writingTimes[REAL] += calc_real_diff;
		
		/** Counts */
		++glyphCount;
		if (glyph->surrogate) {
			++surrogateCount;
		} else {
			if ((convEndian == LITTLE && glyph->bytes[1] == 0) ||
			(convEndian == BIG && glyph->bytes[0] == 0)) {
				if (glyph->bytes[2] == 0 && glyph->bytes[3] == 0) {
					++asciiCount;
				}
			}
		}
		*buf = 0;
	}

	print_verbosity(srcFD);

	free(buf);
	free(glyph);
	quit_converter(srcFD, convFD, EXIT_SUCCESS);
	return 0;
}

void convert_encoding(Glyph* glyph) {
	unsigned int unicode = 0, msb, lsb;
	int i, mask;
	/** Find unicode value of UTF 8 */
	if (srcEncoding == UTF_8) {
		if (glyph->nBytes == 1) {
			unicode = glyph->bytes[0];
		} else {
			mask = 0;
			for (i = 0; i < 7 - glyph->nBytes; ++i) {
				mask <<= 1;
				++mask;
			}
			unicode = glyph->bytes[0] & mask;
			mask = 0x3F;
			for (i = 1; i < glyph->nBytes; ++i) {
				unicode <<= 6;
				unicode += glyph->bytes[i] & mask;
			}
		}
	} 
	/** Find unicode value of UTF 16 */ 
	else {
		if (!glyph->surrogate) {
			unicode = glyph->bytes[0] + (glyph->bytes[1] << 8);
		} else {
			/** TODO - Extra credit */
		}
	}
	memset(glyph, 0, 4);
	/** Use unicode to make new encoding
	* Unicode -> UTF 8 */
	if (convEncoding == UTF_8) {
		/** TODO - Extra credit */
	} 
	/** Unicode -> UTF 16 */
	else {
		/** Surrogate pair */
		if (unicode > 0x10000) {
			unicode -= 0x10000;
			msb = (unicode >> 10) + 0xD800;
			lsb = (unicode & 0x3FF) + 0xDC00;
			unicode = (msb << 16) + lsb;
			mask = 0xFF;
			for (i = 0; i < 4; ++i) {
				glyph->bytes[(i < 2)? i + 2 : i - 2] = 
				(unicode & mask) >> (i * 8);
				mask <<= 8;
			}
			glyph->surrogate = true;
		} else {
			mask = 0xFF;
			glyph->bytes[0] = unicode & mask;
			mask = 0xFF00;
			glyph->bytes[1] = (unicode & mask) >> 8;
			glyph->bytes[2] = glyph->bytes[3] = 0;
		}
	}
}

Glyph* swap_endianness(Glyph* glyph) {
	/* Use XOR to be more efficient with how we swap values. */
	glyph->bytes[0] ^= glyph->bytes[1];
	glyph->bytes[1] ^= glyph->bytes[0];
	glyph->bytes[0] ^= glyph->bytes[1];
	if(glyph->surrogate){  /* If a surrogate pair, swap the next two bytes. */
		glyph->bytes[2] ^= glyph->bytes[3];
		glyph->bytes[3] ^= glyph->bytes[2];
		glyph->bytes[2] ^= glyph->bytes[3];
	}
	return glyph;
}

Glyph* read_utf_8(int fd, Glyph *glyph, unsigned char *buf) {
	int i;
	glyph->bytes[0] = *buf;
	/** 1 Byte? */
	if (*buf >> 7 == 0) {
		glyph->nBytes = 1;
	}
	/** 2 Bytes? */
	else if (*buf >> 5 == 0x6) {
		glyph->nBytes = 2;
	} 
	/** 3 Bytes? */
	else if (*buf >> 4 == 0xE) {
		glyph->nBytes = 3;
	}
	/** 4 Bytes? */
	else if (*buf >> 3 == 0x1E) {
		glyph->nBytes = 4;
	} else {
		free(buf);
		free(glyph);
		print_help(EXIT_FAILURE);
	}
	/** Read the bytes */
	for (i = 1; i < glyph->nBytes; ++i) {
		*buf = 0;
		if (read(fd, buf, 1) && (*buf >> 6 == 2)) {
			glyph->bytes[i] = *buf;
		} else {
			free(buf);
			free(glyph);
			print_help(EXIT_FAILURE);
		}
	}
	return glyph;
}

Glyph* read_utf_16(int fd, Glyph* glyph, unsigned char *buf) {
	unsigned int temp;
	glyph->bytes[0] = *buf;
	if (read(fd, buf, 1) != 1) {
		free(buf);
		free(glyph);
		print_help(EXIT_FAILURE);
	}
	if (srcEndian == LITTLE) {
		glyph->bytes[1] = *buf;
	} else {
		glyph->bytes[1] = glyph->bytes[0];
		glyph->bytes[0] = *buf;
	}
	temp = (glyph->bytes[0] + (glyph->bytes[1] << 8));
	/** Check if this character is a surrogate pair */
	if (temp >= 0xD800 && temp <= 0xDBFF) {
		if (read(fd, &glyph->bytes[2], 1) == 1 && 
		read(fd, &glyph->bytes[3], 1)) {
			if (srcEndian == LITTLE) {
				temp = glyph->bytes[2] + (glyph->bytes[3] << 8);
			} else {
				temp = glyph->bytes[3] + (glyph->bytes[2] << 8);
			}
			if (temp >= 0xDC00 && temp <= 0xDFFF) {
				glyph->surrogate = true;
			} else {
				lseek(fd, -OFFSET, SEEK_CUR);
				glyph->surrogate =false;
			}
		} else {
			free(buf);
			free(glyph);
			print_help(EXIT_FAILURE);
		}
	} else {
		glyph->surrogate = false;
	}
	if (!glyph->surrogate) {
		glyph->bytes[2] = glyph->bytes[3] = 0;
	} else if (srcEndian == BIG) {
		*buf = glyph->bytes[2];
		glyph->bytes[2] = glyph->bytes[3];
		glyph->bytes[3] = *buf;
	}
	return glyph;
}

int read_bom(int* fd) {
	unsigned char buf[2] = {0, 0}; 
	int rv = 0;
	if((rv = read(*fd, &buf[0], 1)) == 1 && 
			(rv = read(*fd, &buf[1], 1)) == 1) { 
		if(buf[0] == 0xff && buf[1] == 0xfe) {
			/*file is little endian*/
			srcEndian = LITTLE; 
			srcEncoding = UTF_16;
		} else if(buf[0] == 0xfe && buf[1] == 0xff) {
			/*file is big endian*/
			srcEndian = BIG;
			srcEncoding = UTF_16;
		} else if (buf[0] == 0xef && buf[1] == 0xbb &&
		(rv = read(*fd, &buf[0], 1) == 1) && buf[0] == 0xbf) {
			srcEncoding = UTF_8;
		} else {
			return 0;
		}
		return 1;
	} else {
		print_help(EXIT_FAILURE);
	}
	return 0;
}

int write_bom(int fd) { 
	int nBytes;
	unsigned char buf[3];
	int rv = 0;
	memset(buf, 0, sizeof(buf));
	if ((fd != STDOUT_FILENO) && (rv = read(fd, &buf[0], 1)) == 1 &&
		(rv = read(fd, &buf[1], 1) == 1)) {
		if(buf[0] == 0xff && buf[1] == 0xfe) {
			if (convEndian == LITTLE && convEncoding == UTF_16) {
				buf[0] = '\n';
				buf[1] = '\0';
				write(fd, buf, 2);
			} else {
				print_help(EXIT_FAILURE);
			}
		} else if (buf[0] == 0xfe && buf[1] == 0xff) {
			if (convEndian == BIG && convEncoding == UTF_16) {
				buf[0] = '\0';
				buf[1] = '\n';
				write(fd, buf, 2);
			} else {
				print_help(EXIT_FAILURE);
			}
		} else if (buf[0] == 0xef && buf[1] == 0xbb &&
		((rv = read(fd, &buf[0], 1)) == 1) && buf[0] == 0xbf) {
			if (convEncoding == UTF_8) {
				buf[0] = '\n';
			} else {
				print_help(EXIT_FAILURE);
			}
		} else {
			print_help(EXIT_FAILURE);
		}
		return 1;
	} else if ((fd != STDOUT_FILENO) && ((rv = read(fd, &buf[0], 1)) == 1 ||
		(rv = read(fd, &buf[1], 1)) == 1)) {
			print_help(EXIT_FAILURE);
	}	
	if (convEncoding == UTF_8) {
		nBytes = 3;
		buf[0] = 0xef;
		buf[1] = 0xbb;
		buf[2] = 0xbf;
	} else {
		nBytes = 2;
		if (convEndian == LITTLE) {
			buf[0] = 0xff;
			buf[1] = 0xfe;
		} else {
			buf[0] = 0xfe;
			buf[1] = 0xff;
		}
	}
	if (write(fd, buf, nBytes) == -1) {
		return 0;
	}
	return 1;
}

void write_glyph(Glyph* glyph, int fd) {
	if (convEncoding == UTF_8) {
		write(fd, glyph->bytes, glyph->nBytes);
	} else {
		if(glyph->surrogate) {
			write(fd, glyph->bytes, SURROGATE_SIZE);
		} else {
			write(fd, glyph->bytes, NON_SURROGATE_SIZE);
		}
	}
}

int parse_args(int argc, char** argv) {
	int option_index;
	char c;
	static struct option long_options[] = {
		{"help", optional_argument, 0, 'h'},
		{"UTF=", required_argument, 0, 'u'},
		{NULL, optional_argument, 0, 'v'},
		{0, 0, 0, 0}
	};
	c = '\0';
	convEncoding = 2;
	convEndian = 2;

	while (optind < argc) {
		while (c != -1 && (c = getopt_long(argc, argv, "hu:v", long_options, 
		&option_index)) != -1) {
			switch(c) { 
				case 'u':
					if (convEncoding == 2) {
						if (strcmp(optarg, "16LE") == 0) {
							convEncoding = UTF_16;
							convEndian = LITTLE;
						} else if (strcmp(optarg, "16BE") == 0) {
							convEncoding = UTF_16;
							convEndian = BIG;
						} else if (strcmp(optarg, "8") == 0) {
							convEncoding = UTF_8;
							convEndian = LITTLE;
						} else {
							print_help(EXIT_FAILURE);
						}
					} else {
						print_help(EXIT_FAILURE);
					}
					break;
				case 'h':
					if (argc == 2) {
						print_help(EXIT_SUCCESS);
					} else {
						print_help(EXIT_FAILURE);
					}
				case 'v':
					if (verbosityLevel == LEVEL_0) {
						verbosityLevel = LEVEL_1;
					} else {
						verbosityLevel = LEVEL_2;
					}
					break;
				case '?':
					print_help(EXIT_FAILURE);
			}
		}
		if (c == -1) {
			if (srcFilename == NULL) {
				srcFilename = calloc(101, sizeof(char));
				strcpy(srcFilename, argv[optind]);
			} else if (convFilename == NULL) {
				convFilename = calloc(101, sizeof(char));
				strcpy(convFilename, argv[optind]);
			} else {
				print_help(EXIT_FAILURE);
			}
			++optind;
		}
	}

	if (srcFilename == NULL) {
		print_help(EXIT_FAILURE);
	}

	if(convEncoding == 2) {
		print_help(EXIT_FAILURE);
	}
	return 1;
}

void print_help(int exit_status) {
	printf("%s", USAGE_ONE);
	printf("%s", USAGE_TWO); 
	quit_converter(NO_FD, NO_FD, exit_status);
}

void print_verbosity(int fd) {
	float fileSize;
	char *srcPWD, hostMachine[201];
	struct utsname osInfo;
	if (verbosityLevel == LEVEL_0) {
		return;
	}
	/** Level 1 + 2 */
	fileSize = (float)lseek(fd, 0, SEEK_END) / 1000.0;
	fprintf(stderr, "  Input file size: %f kb\n", fileSize);

	srcPWD = calloc(101, sizeof(char));
	realpath(srcFilename, srcPWD);
	fprintf(stderr, "  Input file path: %s\n", srcPWD);
	free(srcPWD);

	fprintf(stderr, "  Input file encoding: ");
	if (srcEncoding == UTF_8) {
			fprintf(stderr, "UTF-8\n");
	} else {
		if (srcEndian == LITTLE) {
			fprintf(stderr, "UTF-16LE\n");
		} else {
			fprintf(stderr, "UTF-16BE\n");
		}
	}

	fprintf(stderr, "  Output encoding: ");
	if (convEncoding == UTF_8) {
			fprintf(stderr, "UTF-8\n");
	} else {
		if (convEndian == LITTLE) {
			fprintf(stderr, "UTF-16LE\n");
		} else {
			fprintf(stderr, "UTF-16BE\n");
		}
	}

	hostMachine[200] = '\0';
	gethostname(hostMachine, 200);
	fprintf(stderr, "  Hostmachine: %s\n", hostMachine);

	uname(&osInfo);
	fprintf(stderr, "  Operating System: %s\n", osInfo.sysname);

	if (verbosityLevel == LEVEL_1) {
		return;
	}

	fprintf(stderr, "  Reading: Real: %f, User: %f, Sys: %f\n", 
	readingTimes[REAL], readingTimes[USER], readingTimes[SYS]);

	fprintf(stderr, "  Converting: Real: %f, User: %f, Sys: %f\n", 
	convertingTimes[REAL], convertingTimes[USER], convertingTimes[SYS]);

	fprintf(stderr, "  Writing: Real: %f, User: %f, Sys: %f\n", 
	writingTimes[REAL], writingTimes[USER], writingTimes[SYS]);

	fprintf(stderr, "  ASCII: %d%%\n", (100 * asciiCount) / glyphCount);
	
	fprintf(stderr, "  Surrogates: %d%%\n", (100 * surrogateCount) / glyphCount);

	fprintf(stderr, "  Glyphs: %d\n", glyphCount);
}

void quit_converter(int srcFD, int convFD, int exit_status) {
	free(srcFilename);
	if (convFilename != NULL) {
		free(convFilename);
	}
	close(STDERR_FILENO);
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	if (srcFD != NO_FD)
		close(srcFD);
	if (convFD != NO_FD)
		close(convFD);
	exit(exit_status);
}