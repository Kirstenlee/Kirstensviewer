/*
 * OpenJPEG Private Configuration - Second Life Build
 * 
 * This file contains private/internal configuration settings for OpenJPEG.
 * It is separate from opj_config.h which contains public API settings.
 */

#ifndef OPJ_CONFIG_PRIVATE_H
#define OPJ_CONFIG_PRIVATE_H

/* Include the public configuration */
#include "opj_config.h"

/* ============================================================================
 * Private Build Configuration
 * ============================================================================ */

/* Define if building statically linked */
#if SL_EMBEDDED_OPENJPEG
#  define OPJ_STATIC
#endif

/* Standard library features */
#define OPJ_HAVE_STDINT_H 1
#define OPJ_HAVE_INTTYPES_H 1
#define OPJ_HAVE_STDLIB_H 1
#define OPJ_HAVE_STRING_H 1
#define OPJ_HAVE_MEMORY_H 1

/* Math library features */
#define OPJ_HAVE_FSEEKO 1

/* Threading support (Windows) */
#ifdef _WIN32
#  define OPJ_HAVE_WINDOWS_H 1
#endif

/* Large file support */
#define OPJ_HAVE_FSEEKO 1

/* Endianness */
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#  define OPJ_BIG_ENDIAN
#else
#  define OPJ_LITTLE_ENDIAN
#endif

#endif /* OPJ_CONFIG_PRIVATE_H */
