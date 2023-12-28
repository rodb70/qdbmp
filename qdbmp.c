#include "qdbmp.h"
#include <stdlib.h>
#include <string.h>


/* Bitmap header */
typedef struct _BMP_Header
{
    uint16_t Magic;/* Magic identifier: "BM" */
    uint32_t FileSize;/* Size of the BMP file in bytes */
    uint16_t Reserved1;/* Reserved */
    uint16_t Reserved2;/* Reserved */
    uint32_t DataOffset;/* Offset of image data relative to the file's start */
    uint32_t HeaderSize;/* Size of the header in bytes */
    uint32_t Width;/* Bitmap's width */
    uint32_t Height;/* Bitmap's height */
    uint16_t Planes;/* Number of color planes in the bitmap */
    uint16_t BitsPerPixel;/* Number of bits per pixel */
    uint32_t CompressionType;/* Compression type */
    uint32_t ImageDataSize;/* Size of uncompressed image's data */
    uint32_t HPixelsPerMeter;/* Horizontal resolution (pixels per meter) */
    uint32_t VPixelsPerMeter;/* Vertical resolution (pixels per meter) */
    uint32_t ColorsUsed;/* Number of color indexes in the color table that are actually used by the bitmap */
    uint32_t ColorsRequired;/* Number of color indexes that are required for displaying the bitmap */
} BMP_Header;


/* Private data structure */
struct _BMP
{
    BMP_Header Header;
    uint8_t *Palette;
    uint8_t *Data;
};


/* Holds the last error code */
static BMP_STATUS BMP_LAST_ERROR_CODE = 0;


/* Error description strings */
static const char* BMP_ERROR_STRING[] =
{
    "",
    "General error",
    "Could not allocate enough memory to complete the operation",
    "File input/output error",
    "File not found",
    "File is not a supported BMP variant (must be uncompressed 8, 24 or 32 BPP)",
    "File is not a valid BMP image",
    "An argument is invalid or out of range",
    "The requested action is not compatible with the BMP's type"
};


/* Size of the palette data for 8 BPP bitmaps */
#define BMP_PALETTE_SIZE    ( 256 * 4 )



/*********************************** Forward declarations **********************************/
static int ReadHeader(BMP *bmp, FILE *f);
static int WriteHeader(BMP *bmp, FILE *f);

static int Readuint32_t(uint32_t *x, FILE *f);
static int Readuint16_t(uint16_t *x, FILE *f);

static int Writeuint32_t(uint32_t x, FILE *f);
static int Writeuint16_t(uint16_t x, FILE *f);






/*********************************** Public methods **********************************/

/**************************************************************
    Creates a blank BMP image with the specified dimensions
    and bit depth.
**************************************************************/
BMP* BMP_Create(uint32_t width, uint32_t height, uint16_t depth)
{
    BMP *bmp;
    int bytes_per_pixel = depth >> 3;
    uint32_t bytes_per_row;

    if( height <= 0 || width <= 0 )
    {
        BMP_LAST_ERROR_CODE = BMP_INVALID_ARGUMENT;
        return NULL;
    }

    if( depth != 8 && depth != 24 && depth != 32 )
    {
        BMP_LAST_ERROR_CODE = BMP_FILE_NOT_SUPPORTED;
        return NULL;
    }

/* Allocate the bitmap data structure */
    bmp = calloc( 1, sizeof(BMP) );
    if( bmp == NULL )
    {
        BMP_LAST_ERROR_CODE = BMP_OUT_OF_MEMORY;
        return NULL;
    }

/* Set header' default values */
    bmp->Header.Magic = 0x4D42;
    bmp->Header.Reserved1 = 0;
    bmp->Header.Reserved2 = 0;
    bmp->Header.HeaderSize = 40;
    bmp->Header.Planes = 1;
    bmp->Header.CompressionType = 0;
    bmp->Header.HPixelsPerMeter = 0;
    bmp->Header.VPixelsPerMeter = 0;
    bmp->Header.ColorsUsed = 0;
    bmp->Header.ColorsRequired = 0;

/* Calculate the number of bytes used to store a single image row. This is always
    rounded up to the next multiple of 4. */
    bytes_per_row = width * bytes_per_pixel;
    bytes_per_row += (bytes_per_row % 4 ? 4 - bytes_per_row % 4 : 0);

/* Set header's image specific values */
    bmp->Header.Width = width;
    bmp->Header.Height = height;
    bmp->Header.BitsPerPixel = depth;
    bmp->Header.ImageDataSize = bytes_per_row * height;
    bmp->Header.FileSize = bmp->Header.ImageDataSize + 54 + (depth == 8 ? BMP_PALETTE_SIZE : 0);
    bmp->Header.DataOffset = 54 + (depth == 8 ? BMP_PALETTE_SIZE : 0);

/* Allocate palette */
    if( bmp->Header.BitsPerPixel == 8 )
    {
        bmp->Palette = (uint8_t*) calloc( BMP_PALETTE_SIZE, sizeof(uint8_t) );
        if( bmp->Palette == NULL )
        {
            BMP_LAST_ERROR_CODE = BMP_OUT_OF_MEMORY;
            free( bmp );
            return NULL;
        }
    }
    else
    {
        bmp->Palette = NULL;
    }

/* Allocate pixels */
    bmp->Data = (uint8_t*) calloc( bmp->Header.ImageDataSize, sizeof(uint8_t) );
    if( bmp->Data == NULL )
    {
        BMP_LAST_ERROR_CODE = BMP_OUT_OF_MEMORY;
        free( bmp->Palette );
        free( bmp );
        return NULL;
    }

    BMP_LAST_ERROR_CODE = BMP_OK;

    return bmp;
}

/**************************************************************
    Frees all the memory used by the specified BMP image.
**************************************************************/
void BMP_Free(BMP *bmp)
{
    if( bmp == NULL )
    {
        return;
    }

    if( bmp->Palette != NULL )
    {
        free( bmp->Palette );
    }

    if( bmp->Data != NULL )
    {
        free( bmp->Data );
    }

    free( bmp );

    BMP_LAST_ERROR_CODE = BMP_OK;
}

/**************************************************************
    Reads the specified BMP image file.
**************************************************************/
BMP* BMP_ReadFile(const char *filename)
{
    BMP *bmp;
    FILE *f;

    if( filename == NULL )
    {
        BMP_LAST_ERROR_CODE = BMP_INVALID_ARGUMENT;
        return NULL;
    }

    /* Allocate */
    bmp = calloc( 1, sizeof(BMP) );
    if( bmp == NULL )
    {
        BMP_LAST_ERROR_CODE = BMP_OUT_OF_MEMORY;
        return NULL;
    }

    /* Open file */
    f = fopen( filename, "rb" );
    if( f == NULL )
    {
        BMP_LAST_ERROR_CODE = BMP_FILE_NOT_FOUND;
        free( bmp );
        return NULL;
    }

    /* Read header */
    if( ReadHeader( bmp, f ) != BMP_OK || bmp->Header.Magic != 0x4D42 )
    {
        BMP_LAST_ERROR_CODE = BMP_FILE_INVALID;
        fclose( f );
        free( bmp );
        return NULL;
    }

    /* Verify that the bitmap variant is supported */
    if(( bmp->Header.BitsPerPixel != 32 && bmp->Header.BitsPerPixel != 24 &&
         bmp->Header.BitsPerPixel != 8) || bmp->Header.CompressionType != 0 ||
                                           bmp->Header.HeaderSize != 40 )
    {
        BMP_LAST_ERROR_CODE = BMP_FILE_NOT_SUPPORTED;
        fclose( f );
        free( bmp );
        return NULL;
    }

    /* Allocate and read palette */
    if( bmp->Header.BitsPerPixel == 8 )
    {
        bmp->Palette = (uint8_t*) malloc( BMP_PALETTE_SIZE * sizeof(uint8_t) );
        if( bmp->Palette == NULL )
        {
            BMP_LAST_ERROR_CODE = BMP_OUT_OF_MEMORY;
            fclose( f );
            free( bmp );
            return NULL;
        }

        if( fread( bmp->Palette, sizeof(uint8_t), BMP_PALETTE_SIZE, f ) != BMP_PALETTE_SIZE )
        {
            BMP_LAST_ERROR_CODE = BMP_FILE_INVALID;
            fclose( f );
            free( bmp->Palette );
            free( bmp );
            return NULL;
        }
    }
    else/* Not an indexed image */
    {
        bmp->Palette = NULL;
    }

    /* Allocate memory for image data */
    bmp->Data = (uint8_t*) malloc( bmp->Header.ImageDataSize );
    if( bmp->Data == NULL )
    {
        BMP_LAST_ERROR_CODE = BMP_OUT_OF_MEMORY;
        fclose( f );
        free( bmp->Palette );
        free( bmp );
        return NULL;
    }

    /* Read image data */
    /* read the bitmap in reverse order */
    for( int idx = (bmp->Header.Height - 1) * bmp->Header.Width; idx >= 0; idx -= bmp->Header.Width )
    {
        if( fread( bmp->Data + idx, sizeof(uint8_t), bmp->Header.Width, f ) != bmp->Header.Width )
        {
            BMP_LAST_ERROR_CODE = BMP_FILE_INVALID;
            fclose( f );
            free( bmp->Data );
            free( bmp->Palette );
            free( bmp );
            return NULL;
        }
        if( 0 != ( bmp->Header.Width % sizeof( uint32_t)))
        {
            uint8_t leftover[ sizeof( uint32_t)] = {0};
            uint32_t remainder = sizeof( uint32_t) - ( bmp->Header.Width % sizeof( uint32_t));
            size_t rslt_read = fread( leftover, sizeof(uint8_t), remainder, f );
            (void)rslt_read;
        }
    }


    fclose( f );

    BMP_LAST_ERROR_CODE = BMP_OK;

    return bmp;
}

/**************************************************************
    Writes the BMP image to the specified file.
**************************************************************/
void BMP_WriteFile(BMP *bmp, const char *filename)
{
    FILE *f;

    if( filename == NULL )
    {
        BMP_LAST_ERROR_CODE = BMP_INVALID_ARGUMENT;
        return;
    }

    /* Open file */
    f = fopen( filename, "wb" );
    if( f == NULL )
    {
        BMP_LAST_ERROR_CODE = BMP_FILE_NOT_FOUND;
        return;
    }

    /* Write header */
    if( WriteHeader( bmp, f ) != BMP_OK )
    {
        BMP_LAST_ERROR_CODE = BMP_IO_ERROR;
        fclose( f );
        return;
    }

    /* Write palette */
    if( bmp->Palette )
    {
        if( fwrite( bmp->Palette, sizeof(uint8_t), BMP_PALETTE_SIZE, f ) != BMP_PALETTE_SIZE )
        {
            BMP_LAST_ERROR_CODE = BMP_IO_ERROR;
            fclose( f );
            return;
        }
    }

    /* Write data */
    for( int idx = (bmp->Header.Height - 1) * bmp->Header.Width; idx >= 0; idx -= bmp->Header.Width )
    {
        if( fwrite( bmp->Data + idx, sizeof(uint8_t), bmp->Header.Width, f ) != bmp->Header.ImageDataSize )
        {
            BMP_LAST_ERROR_CODE = BMP_IO_ERROR;
            fclose( f );
            return;
        }
        /* reads are 32-bit aligned */
        if( 0 != ( bmp->Header.Width % sizeof( uint32_t)))
        {
            uint8_t leftover[ sizeof( uint32_t)] = {0};
            uint32_t remainder = sizeof( uint32_t) - ( bmp->Header.Width % sizeof( uint32_t));
            fwrite( leftover, sizeof(uint8_t), remainder, f );
        }
    }

    BMP_LAST_ERROR_CODE = BMP_OK;
    fclose( f );
}

/**************************************************************
    Returns the image's width.
**************************************************************/
uint32_t BMP_GetWidth(BMP *bmp)
{
    if( bmp == NULL )
    {
        BMP_LAST_ERROR_CODE = BMP_INVALID_ARGUMENT;
        return -1;
    }

    BMP_LAST_ERROR_CODE = BMP_OK;

    return (bmp->Header.Width);
}

/**************************************************************
    Returns the image's height.
**************************************************************/
uint32_t BMP_GetHeight(BMP *bmp)
{
    if( bmp == NULL )
    {
        BMP_LAST_ERROR_CODE = BMP_INVALID_ARGUMENT;
        return -1;
    }

    BMP_LAST_ERROR_CODE = BMP_OK;

    return (bmp->Header.Height);
}

/**************************************************************
    Returns the image's color depth (bits per pixel).
**************************************************************/
uint16_t BMP_GetDepth(BMP *bmp)
{
    if( bmp == NULL )
    {
        BMP_LAST_ERROR_CODE = BMP_INVALID_ARGUMENT;
        return -1;
    }

    BMP_LAST_ERROR_CODE = BMP_OK;

    return (bmp->Header.BitsPerPixel);
}

/**************************************************************
    Populates the arguments with the specified pixel's RGB
    values.
**************************************************************/
void BMP_GetPixelRGB(BMP *bmp, uint32_t x, uint32_t y, uint8_t *r, uint8_t *g, uint8_t *b)
{
    uint8_t *pixel;
    uint32_t bytes_per_row;
    uint8_t bytes_per_pixel;

    if( bmp == NULL || x >= bmp->Header.Width || y >= bmp->Header.Height )
    {
        BMP_LAST_ERROR_CODE = BMP_INVALID_ARGUMENT;
    }
    else
    {
        BMP_LAST_ERROR_CODE = BMP_OK;

        bytes_per_pixel = bmp->Header.BitsPerPixel >> 3;

        /* Row's size is rounded up to the next multiple of 4 bytes */
        bytes_per_row = bmp->Header.ImageDataSize / bmp->Header.Height;

        /* Calculate the location of the relevant pixel (rows are flipped) */
        pixel = bmp->Data + ((bmp->Header.Height - y - 1) * bytes_per_row + x * bytes_per_pixel);

        /* In indexed color mode the pixel's value is an index within the palette */
        if( bmp->Header.BitsPerPixel == 8 )
        {
            pixel = bmp->Palette + *pixel * 4;
        }

/* Note: colors are stored in BGR order */
        if( r )
            *r = *(pixel + 2);
        if( g )
            *g = *(pixel + 1);
        if( b )
            *b = *(pixel + 0);
    }
}

/**************************************************************
    Sets the specified pixel's RGB values.
**************************************************************/
void BMP_SetPixelRGB(BMP *bmp, uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t *pixel;
    uint32_t bytes_per_row;
    uint8_t bytes_per_pixel;

    if( bmp == NULL || x >= bmp->Header.Width || y >= bmp->Header.Height )
    {
        BMP_LAST_ERROR_CODE = BMP_INVALID_ARGUMENT;
    }

    else if( bmp->Header.BitsPerPixel != 24 && bmp->Header.BitsPerPixel != 32 )
    {
        BMP_LAST_ERROR_CODE = BMP_TYPE_MISMATCH;
    }

    else
    {
        BMP_LAST_ERROR_CODE = BMP_OK;

        bytes_per_pixel = bmp->Header.BitsPerPixel >> 3;

        /* Row's size is rounded up to the next multiple of 4 bytes */
        bytes_per_row = bmp->Header.ImageDataSize / bmp->Header.Height;

        /* Calculate the location of the relevant pixel (rows are flipped) */
        pixel = bmp->Data + ((bmp->Header.Height - y - 1) * bytes_per_row + x * bytes_per_pixel);

        /* Note: colors are stored in BGR order */
        *(pixel + 2) = r;
        *(pixel + 1) = g;
        *(pixel + 0) = b;
    }
}

/**************************************************************
    Gets the specified pixel's color index.
**************************************************************/
void BMP_GetPixelIndex(BMP *bmp, uint32_t x, uint32_t y, uint8_t *val)
{
    uint8_t *pixel;
    uint32_t bytes_per_row;

    if( bmp == NULL || x >= bmp->Header.Width || y >= bmp->Header.Height )
    {
        BMP_LAST_ERROR_CODE = BMP_INVALID_ARGUMENT;
    }

    else if( bmp->Header.BitsPerPixel != 8 )
    {
        BMP_LAST_ERROR_CODE = BMP_TYPE_MISMATCH;
    }

    else
    {
        BMP_LAST_ERROR_CODE = BMP_OK;

/* Row's size is rounded up to the next multiple of 4 bytes */
        bytes_per_row = bmp->Header.ImageDataSize / bmp->Header.Height;

/* Calculate the location of the relevant pixel */
        pixel = bmp->Data + ((bmp->Header.Height - y - 1) * bytes_per_row + x);

        if( val )
            *val = *pixel;
    }
}

/**************************************************************
    Sets the specified pixel's color index.
**************************************************************/
void BMP_SetPixelIndex(BMP *bmp, uint32_t x, uint32_t y, uint8_t val)
{
    uint8_t *pixel;
    uint32_t bytes_per_row;

    if( bmp == NULL || x >= bmp->Header.Width || y >= bmp->Header.Height )
    {
        BMP_LAST_ERROR_CODE = BMP_INVALID_ARGUMENT;
    }

    else if( bmp->Header.BitsPerPixel != 8 )
    {
        BMP_LAST_ERROR_CODE = BMP_TYPE_MISMATCH;
    }

    else
    {
        BMP_LAST_ERROR_CODE = BMP_OK;

        /* Row's size is rounded up to the next multiple of 4 bytes */
        bytes_per_row = bmp->Header.ImageDataSize / bmp->Header.Height;

        /* Calculate the location of the relevant pixel */
        pixel = bmp->Data + ((bmp->Header.Height - y - 1) * bytes_per_row + x);

        *pixel = val;
    }
}

/**************************************************************
    Gets the color value for the specified palette index.
**************************************************************/
void BMP_GetPaletteColor(BMP *bmp, uint8_t index, uint8_t *r, uint8_t *g, uint8_t *b)
{
    if( bmp == NULL )
    {
        BMP_LAST_ERROR_CODE = BMP_INVALID_ARGUMENT;
    }

    else if( bmp->Header.BitsPerPixel != 8 )
    {
        BMP_LAST_ERROR_CODE = BMP_TYPE_MISMATCH;
    }

    else
    {
        if( r )
            *r = *(bmp->Palette + index * 4 + 2);
        if( g )
            *g = *(bmp->Palette + index * 4 + 1);
        if( b )
            *b = *(bmp->Palette + index * 4 + 0);

        BMP_LAST_ERROR_CODE = BMP_OK;
    }
}

/**************************************************************
    Sets the color value for the specified palette index.
**************************************************************/
void BMP_SetPaletteColor(BMP *bmp, uint8_t index, uint8_t r, uint8_t g, uint8_t b)
{
    if( bmp == NULL )
    {
        BMP_LAST_ERROR_CODE = BMP_INVALID_ARGUMENT;
    }

    else if( bmp->Header.BitsPerPixel != 8 )
    {
        BMP_LAST_ERROR_CODE = BMP_TYPE_MISMATCH;
    }

    else
    {
        *(bmp->Palette + index * 4 + 2) = r;
        *(bmp->Palette + index * 4 + 1) = g;
        *(bmp->Palette + index * 4 + 0) = b;

        BMP_LAST_ERROR_CODE = BMP_OK;
    }
}

void * BMP_GetPixelData(BMP *bmp)
{
    return bmp->Data;
}

void * BMP_GetPalletteData(BMP *bmp)
{
    return bmp->Palette;
}

/**************************************************************
    Returns the last error code.
**************************************************************/
BMP_STATUS BMP_GetError()
{
    return BMP_LAST_ERROR_CODE;
}

/**************************************************************
    Returns a description of the last error code.
**************************************************************/
const char* BMP_GetErrorDescription()
{
    if( BMP_LAST_ERROR_CODE > 0 && BMP_LAST_ERROR_CODE < BMP_ERROR_NUM )
    {
        return BMP_ERROR_STRING[BMP_LAST_ERROR_CODE];
    }
    else
    {
        return NULL;
    }
}

/*********************************** Private methods **********************************/

/**************************************************************
    Reads the BMP file's header into the data structure.
    Returns BMP_OK on success.
**************************************************************/
static int ReadHeader(BMP *bmp, FILE *f)
{
    if( bmp == NULL || f == NULL )
    {
        return BMP_INVALID_ARGUMENT;
    }

/* The header's fields are read one by one, and converted from the format's
    little endian to the system's native representation. */
    if( !Readuint16_t( &(bmp->Header.Magic), f ) )
        return BMP_IO_ERROR;
    if( !Readuint32_t( &(bmp->Header.FileSize), f ) )
        return BMP_IO_ERROR;
    if( !Readuint16_t( &(bmp->Header.Reserved1), f ) )
        return BMP_IO_ERROR;
    if( !Readuint16_t( &(bmp->Header.Reserved2), f ) )
        return BMP_IO_ERROR;
    if( !Readuint32_t( &(bmp->Header.DataOffset), f ) )
        return BMP_IO_ERROR;
    if( !Readuint32_t( &(bmp->Header.HeaderSize), f ) )
        return BMP_IO_ERROR;
    if( !Readuint32_t( &(bmp->Header.Width), f ) )
        return BMP_IO_ERROR;
    if( !Readuint32_t( &(bmp->Header.Height), f ) )
        return BMP_IO_ERROR;
    if( !Readuint16_t( &(bmp->Header.Planes), f ) )
        return BMP_IO_ERROR;
    if( !Readuint16_t( &(bmp->Header.BitsPerPixel), f ) )
        return BMP_IO_ERROR;
    if( !Readuint32_t( &(bmp->Header.CompressionType), f ) )
        return BMP_IO_ERROR;
    if( !Readuint32_t( &(bmp->Header.ImageDataSize), f ) )
        return BMP_IO_ERROR;
    if( !Readuint32_t( &(bmp->Header.HPixelsPerMeter), f ) )
        return BMP_IO_ERROR;
    if( !Readuint32_t( &(bmp->Header.VPixelsPerMeter), f ) )
        return BMP_IO_ERROR;
    if( !Readuint32_t( &(bmp->Header.ColorsUsed), f ) )
        return BMP_IO_ERROR;
    if( !Readuint32_t( &(bmp->Header.ColorsRequired), f ) )
        return BMP_IO_ERROR;

    return BMP_OK;
}

/**************************************************************
    Writes the BMP file's header into the data structure.
    Returns BMP_OK on success.
**************************************************************/
static int WriteHeader(BMP *bmp, FILE *f)
{
    if( bmp == NULL || f == NULL )
    {
        return BMP_INVALID_ARGUMENT;
    }

/* The header's fields are written one by one, and converted to the format's
    little endian representation. */
    if( !Writeuint16_t( bmp->Header.Magic, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint32_t( bmp->Header.FileSize, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint16_t( bmp->Header.Reserved1, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint16_t( bmp->Header.Reserved2, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint32_t( bmp->Header.DataOffset, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint32_t( bmp->Header.HeaderSize, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint32_t( bmp->Header.Width, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint32_t( bmp->Header.Height, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint16_t( bmp->Header.Planes, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint16_t( bmp->Header.BitsPerPixel, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint32_t( bmp->Header.CompressionType, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint32_t( bmp->Header.ImageDataSize, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint32_t( bmp->Header.HPixelsPerMeter, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint32_t( bmp->Header.VPixelsPerMeter, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint32_t( bmp->Header.ColorsUsed, f ) )
        return BMP_IO_ERROR;
    if( !Writeuint32_t( bmp->Header.ColorsRequired, f ) )
        return BMP_IO_ERROR;

    return BMP_OK;
}

/**************************************************************
    Reads a little-endian unsigned int from the file.
    Returns non-zero on success.
**************************************************************/
static int Readuint32_t(uint32_t *x, FILE *f)
{
    uint8_t little[4];/* BMPs use 32 bit ints */

    if( x == NULL || f == NULL )
    {
        return 0;
    }

    if( fread( little, 4, 1, f ) != 1 )
    {
        return 0;
    }

    *x = (little[3] << 24 | little[2] << 16 | little[1] << 8 | little[0]);

    return 1;
}

/**************************************************************
    Reads a little-endian unsigned short int from the file.
    Returns non-zero on success.
**************************************************************/
static int Readuint16_t(uint16_t *x, FILE *f)
{
    uint8_t little[2];/* BMPs use 16 bit shorts */

    if( x == NULL || f == NULL )
    {
        return 0;
    }

    if( fread( little, 2, 1, f ) != 1 )
    {
        return 0;
    }

    *x = (little[1] << 8 | little[0]);

    return 1;
}

/**************************************************************
    Writes a little-endian unsigned int to the file.
    Returns non-zero on success.
**************************************************************/
static int Writeuint32_t(uint32_t x, FILE *f)
{
    uint8_t little[4];/* BMPs use 32 bit ints */

    little[3] = (uint8_t) ((x & 0xff000000) >> 24);
    little[2] = (uint8_t) ((x & 0x00ff0000) >> 16);
    little[1] = (uint8_t) ((x & 0x0000ff00) >> 8);
    little[0] = (uint8_t) ((x & 0x000000ff) >> 0);

    return (f && fwrite( little, 4, 1, f ) == 1);
}

/**************************************************************
    Writes a little-endian unsigned short int to the file.
    Returns non-zero on success.
**************************************************************/
static int Writeuint16_t(uint16_t x, FILE *f)
{
    uint8_t little[2];/* BMPs use 16 bit shorts */

    little[1] = (uint8_t) ((x & 0xff00) >> 8);
    little[0] = (uint8_t) ((x & 0x00ff) >> 0);

    return (f && fwrite( little, 2, 1, f ) == 1);
}

