/******************************************************************************/
/*                                                                            */
/*  \file       msgpack.c                                                     */
/*  \date       Oct 2018                                                      */
/*  \author     Severin Leven, based on code by Julien Lefrique               */
/*                                                                            */
/*  \brief      MessagePack object serialization library (see msgpack.org)    */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  Target CPU: any 32bit                                                     */
/*  Ressources: CPU                                                           */
/*                                                                            */
/******************************************************************************/


//----------//
// Includes //
//----------//

#include "msgpack.h"
#include "lasso_errno.h"
#include <string.h>


//----------------------------//
// Constant/Macro Definitions //
//----------------------------//

// Nil
#define PACK_TYPE_NIL                       (0xC0U)
// Booleans
#define PACK_TYPE_BOOLEAN_FALSE             (0xC2U)
#define PACK_TYPE_BOOLEAN_TRUE              (0xC3U)
// Integers
#define PACK_TYPE_FIXNUM_POSITIVE           (0x00U)
#define PACK_TYPE_UINT8                     (0xCCU)
#define PACK_TYPE_UINT16                    (0xCDU)
#define PACK_TYPE_UINT32                    (0xCEU)
#define PACK_TYPE_UINT64                    (0xCFU)
#define PACK_TYPE_FIXNUM_NEGATIVE           (0xE0U)
#define PACK_TYPE_INT8                      (0xD0U)
#define PACK_TYPE_INT16                     (0xD1U)
#define PACK_TYPE_INT32                     (0xD2U)
#define PACK_TYPE_INT64                     (0xD3U)
// Floating point
#define PACK_TYPE_FLOAT                     (0xCAU)
#define PACK_TYPE_DOUBLE                    (0xCBU)
// Raw bytes
#define PACK_TYPE_RAW_FIX                   (0xA0U)
#define PACK_TYPE_RAW_8                     (0xD9U)
#define PACK_TYPE_RAW_16                    (0xDAU)
#define PACK_TYPE_RAW_32                    (0xDBU)
// Arrays
#define PACK_TYPE_ARRAY_FIX                 (0x90U)
#define PACK_TYPE_ARRAY_16                  (0xDCU)
#define PACK_TYPE_ARRAY_32                  (0xDDU)
// Maps
#define PACK_TYPE_MAP_FIX                   (0x80U)
#define PACK_TYPE_MAP_16                    (0xDEU)
#define PACK_TYPE_MAP_32                    (0xDFU)

// Integers ranges
#define PACK_TYPE_INT8_MAX                  (127)
#define PACK_TYPE_INT8_MIN                  (-128)
#define PACK_TYPE_INT16_MAX                 (32767)
#define PACK_TYPE_INT16_MIN                 (-32767-1)
#define PACK_TYPE_INT32_MAX                 (2147483647L)
#define PACK_TYPE_INT32_MIN                 (-2147483647L-1)
#define PACK_TYPE_UINT8_MAX                 (255U)
#define PACK_TYPE_UINT16_MAX                (65535U)
#define PACK_TYPE_UINT32_MAX                (4294967295UL)

// Fixnum ranges
#define PACK_TYPE_POSITIVE_FIXNUM_MAX       (127U)
#define PACK_TYPE_NEGATIVE_FIXNUM_MIN       (-32)
#define PACK_TYPE_NEGATIVE_FIXNUM_MAX       (-1)


//----------------------//
// Function definitions //
//----------------------//

static bool PackReaderIsEmpty (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    T_PackLen len                   //!< Data length.
) {
    if ((reader->cursor - reader->buffer_start + len) <= reader->buffer_len) {
        return false;
    }
    else {
        return true;
    }
}

T_PackType PackGetType (
    struct S_PackReader* reader     //!< Pointer to pack reader.
) {
    uint8_t type;

    if (PackReaderIsEmpty(reader, 1)) {
        return E_PackTypeUnknown;
    }

    type = reader->cursor[0];

    if (type == PACK_TYPE_NIL) {
        return E_PackTypeNil;
    }
    else
    if (type == PACK_TYPE_BOOLEAN_FALSE ||
        type == PACK_TYPE_BOOLEAN_TRUE) {
        return E_PackTypeBoolean;
    }
    else
    if ((type & 0x80) == PACK_TYPE_FIXNUM_POSITIVE ||
        type == PACK_TYPE_UINT8  ||
        type == PACK_TYPE_UINT16 ||
        type == PACK_TYPE_UINT32 ||
        type == PACK_TYPE_UINT64) {
        return E_PackTypeUnsignedInteger;
    }
    else
    if ((type & 0xE0) == PACK_TYPE_FIXNUM_NEGATIVE ||
        type == PACK_TYPE_INT8  ||
        type == PACK_TYPE_INT16 ||
        type == PACK_TYPE_INT32 ||
        type == PACK_TYPE_INT64) {
        return E_PackTypeSignedInteger;
    }
    else
    if (type == PACK_TYPE_FLOAT || type == PACK_TYPE_DOUBLE) {
        return E_PackTypeFloat;
    }
    else
    if ((type & 0xE0) == PACK_TYPE_RAW_FIX ||
        type == PACK_TYPE_RAW_16 ||
        type == PACK_TYPE_RAW_32) {
        return E_PackTypeRawBytes;
    }
    else
    if ((type & 0xF0) == PACK_TYPE_ARRAY_FIX ||
        type == PACK_TYPE_ARRAY_16 ||
        type == PACK_TYPE_ARRAY_32) {
        return E_PackTypeArray;
    }
    else
    if ((type & 0xF0) == PACK_TYPE_MAP_FIX ||
        type == PACK_TYPE_MAP_16 ||
        type == PACK_TYPE_MAP_32) {
        return E_PackTypeMap;
    }
    else {
        return E_PackTypeUnknown;
    }
}

void PackReaderSetBuffer (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    uint8_t* buffer,                //!< Buffer that contains the serialized data.
    T_PackLen len                   //!< Length.
) {
    reader->buffer_start = buffer;
    reader->buffer_len   = len;
    reader->cursor       = buffer;
}

int32_t PackReaderOpen (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    T_PackType type,                //!< Type.
    T_PackLen* length               //!< Number of elements.
) {
    T_PackLen len;
    uint8_t hdr;

    if (type != E_PackTypeArray && type != E_PackTypeMap) {
        return EILSEQ;
    }

    if (type != PackGetType(reader)) {
        return EINVAL;
    }

    len = 0;
    hdr = reader->cursor[0];

    if (((hdr & 0xF0) == PACK_TYPE_ARRAY_FIX  || (hdr & 0xF0) == PACK_TYPE_MAP_FIX) &&
        !PackReaderIsEmpty(reader, 1)) {
        len = (T_PackLen)(reader->cursor[0] & ~0xF0);
        reader->cursor += 1;
    }
    else
    if ((hdr == PACK_TYPE_ARRAY_16 || hdr == PACK_TYPE_MAP_16) &&
        !PackReaderIsEmpty(reader, 3)) {
        len = (T_PackLen)((uint16_t)reader->cursor[1] << 8 | reader->cursor[2] << 0);
        reader->cursor += 3;
    }
    else
    if ((hdr == PACK_TYPE_ARRAY_32 || hdr == PACK_TYPE_MAP_32) &&
        !PackReaderIsEmpty(reader, 5)) {
        len = (T_PackLen)((uint32_t)reader->cursor[1] << 24 | (uint32_t)reader->cursor[2] << 16 |
                          (uint16_t)reader->cursor[3] << 8  | reader->cursor[4] << 0);
        reader->cursor += 5;
    }

    *length = len;

    return 0;
}

int32_t PackReaderIsNil (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    bool * is_nil                   //!< TRUE if a Nil, FALSE if not.
) {
    if (PackReaderIsEmpty(reader, 1)) {
        return EIO;
    }

    if (PackGetType(reader) == E_PackTypeNil) {
        reader->cursor += 1;
        *is_nil = true;
    }
    else {
        *is_nil = false;
    }

    return 0;
}

int32_t PackReaderGetBoolean (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    bool* is_true                   //!< Pointer to boolean.
) {
    uint8_t type;

    if (PackReaderIsEmpty(reader, 1)) {
        return EIO;
    }

    if (PackGetType(reader) != E_PackTypeBoolean) {
        return EINVAL;
    }

    type = reader->cursor[0];
    reader->cursor += 1;
    *is_true = (type == PACK_TYPE_BOOLEAN_TRUE) ? true : false;

    return 0;
}

int32_t PackReaderGetSignedChar (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    int8_t* value                   //!< Pointer to signed char.
) {
    uint8_t type;

    if (PackReaderIsEmpty(reader, 1)) {
        return EIO;
    }

    type = reader->cursor[0];

    if ((type & 0x80) == PACK_TYPE_FIXNUM_POSITIVE || (type & 0xE0) == PACK_TYPE_FIXNUM_NEGATIVE) {
        *value =  reader->cursor[0];
        reader->cursor += 1;
    }
    else
    if (type == PACK_TYPE_INT8 && !PackReaderIsEmpty(reader, 2)) {
        *value = reader->cursor[1];
        reader->cursor += 2;
    }
    else {
        return EINVAL;
    }

    return 0;
}

int32_t PackReaderGetSignedShort (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    int16_t* value                  //!< Pointer to signed short.
) {
    uint8_t type;

    if (PackReaderIsEmpty(reader, 1)) {
        return EIO;
    }

    type = reader->cursor[0];

    if (type == PACK_TYPE_INT16 && !PackReaderIsEmpty(reader, 3)) {
        // Cast to extend to sign bit
        *value = (int16_t)((uint16_t)reader->cursor[1] << 8 | reader->cursor[2]);
        reader->cursor += 3;
    }
    else {
        return EINVAL;
    }

    return 0;
}

int32_t PackReaderGetSignedLong (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    int32_t* value                  //!< Pointer to signed long.
) {
    uint8_t type;

    if (PackReaderIsEmpty(reader, 1)) {
        return EIO;
    }

    type = reader->cursor[0];

    if (type == PACK_TYPE_INT32 && !PackReaderIsEmpty(reader, 5)) {
        // Cast to extend to sign bit
        *value = (int32_t)((uint32_t)reader->cursor[1] << 24 | (uint32_t)reader->cursor[2] << 16 |
                           (uint16_t)reader->cursor[3] << 8  | reader->cursor[4]);
        reader->cursor += 5;
    }
    else {
        return EINVAL;
    }

    return 0;
}

int32_t PackReaderGetSignedInteger (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    int32_t* value                  //!< Pointer to signed integer.
) {
    uint8_t type;
    int32_t integer;

    if (PackReaderIsEmpty(reader, 1)) {
        return EIO;
    }

    if (PackGetType(reader) != E_PackTypeSignedInteger)
    {
        // A positive integer is likely serialized as an unsigned integer by the peer
        int err;
        uint32_t _value;

        err = PackReaderGetUnsignedInteger(reader, &_value);
        *value = (int32_t)_value;
        return err;
    }

    type = reader->cursor[0];

    if ((type & 0x80) == PACK_TYPE_FIXNUM_POSITIVE || (type & 0xE0) == PACK_TYPE_FIXNUM_NEGATIVE) {
        integer =  (int32_t)((int8_t)reader->cursor[0]);
        reader->cursor += 1;
    }
    else
    if (type == PACK_TYPE_INT8 && !PackReaderIsEmpty(reader, 2)) {
        integer = (int32_t)((int8_t)reader->cursor[1]);
        reader->cursor += 2;
    }
    else
    if (type == PACK_TYPE_INT16 && !PackReaderIsEmpty(reader, 3)) {
        // Cast to extend to sign bit
        integer = (int32_t)((uint16_t)reader->cursor[1] << 8 | reader->cursor[2]);
        reader->cursor += 3;
    }
    else
    if (type == PACK_TYPE_INT32 && !PackReaderIsEmpty(reader, 5)) {
        // Cast to extend to sign bit
        integer = (int32_t)((uint32_t)reader->cursor[1] << 24 | (uint32_t)reader->cursor[2] << 16 |
                            (uint16_t)reader->cursor[3] << 8  | reader->cursor[4]);
        reader->cursor += 5;
    }
    else
    if (type == PACK_TYPE_INT64 && !PackReaderIsEmpty(reader, 9)) {
        // Not supported
        reader->cursor += 9;
        return ENOTSUP;
    }
    else {
        return EINVAL;
    }

    *value = integer;

    return 0;
}

int32_t PackReaderGetUnsignedChar (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    uint8_t* value                  //!< Pointer to unsigned char.
) {
    uint8_t type;

    if (PackReaderIsEmpty(reader, 1)) {
        return EIO;
    }

    type = reader->cursor[0];

    if ((type & 0x80) == PACK_TYPE_FIXNUM_POSITIVE) {
        *value = reader->cursor[0];
        reader->cursor += 1;
    }
    else
    if (type == PACK_TYPE_UINT8 && !PackReaderIsEmpty(reader, 2)) {
        *value =  reader->cursor[1];
        reader->cursor += 2;
    }
    else {
        return EINVAL;
    }

    return 0;
}

int32_t PackReaderGetUnsignedShort (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    uint16_t* value                 //!< Pointer to unsigned short.
) {
    uint8_t type;

    if (PackReaderIsEmpty(reader, 1)) {
        return EIO;
    }

    type = reader->cursor[0];

    if (type == PACK_TYPE_UINT16 && !PackReaderIsEmpty(reader, 3)) {
        *value = (uint16_t)reader->cursor[1] << 8 | reader->cursor[2];
        reader->cursor += 3;
    }
    else {
        return EINVAL;
    }

    return 0;
}

int32_t PackReaderGetUnsignedLong (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    uint32_t* value                 //!< Pointer to unsigned long.
) {
    uint8_t type;

    if (PackReaderIsEmpty(reader, 1)) {
        return EIO;
    }

    type = reader->cursor[0];

    if (type == PACK_TYPE_UINT32 && !PackReaderIsEmpty(reader, 5)) {
        *value = (uint32_t)reader->cursor[1] << 24 | (uint32_t)reader->cursor[2] << 16 |
                 (uint16_t)reader->cursor[3] << 8  | reader->cursor[4];
        reader->cursor += 5;
    }
    else {
        return EINVAL;
    }

    return 0;
}

int32_t PackReaderGetUnsignedInteger (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    uint32_t* value                 //!< Pointer to unsigned integer.
) {
    uint8_t type;
    uint32_t integer;

    if (PackReaderIsEmpty(reader, 1)) {
        return EIO;
    }

    if (PackGetType(reader) != E_PackTypeUnsignedInteger) {
        return EINVAL;
    }

    integer = 0;
    type = reader->cursor[0];

    if ((type & 0x80) == PACK_TYPE_FIXNUM_POSITIVE) {
        integer = (uint32_t)(reader->cursor[0]);
        reader->cursor += 1;
    }
    else
    if (type == PACK_TYPE_UINT8 && !PackReaderIsEmpty(reader, 2)) {
        integer =  (uint32_t)(reader->cursor[1]);
        reader->cursor += 2;
    }
    else
    if (type == PACK_TYPE_UINT16 && !PackReaderIsEmpty(reader, 3)) {
        integer = (uint32_t)((uint16_t)reader->cursor[1] << 8 | reader->cursor[2]);
        reader->cursor += 3;
    }
    else
    if (type == PACK_TYPE_UINT32 && !PackReaderIsEmpty(reader, 5)) {
        integer = (uint32_t)reader->cursor[1] << 24 | (uint32_t)reader->cursor[2] << 16 |
                  (uint16_t)reader->cursor[3] << 8  | reader->cursor[4];
        reader->cursor += 5;
    }
    else
    if (type == PACK_TYPE_UINT64 && !PackReaderIsEmpty(reader, 9)) {
        // Not supported
        reader->cursor += 9;
        return ENOTSUP;
    }
    else
    {
        return EINVAL;
    }

    *value = integer;

    return 0;
}

int32_t PackReaderGetFloat (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    T_PackFloat* value              //!< Pointer to float.
) {
    uint8_t type;
    union {
        uint32_t i;
        T_PackFloat f;
    } fi;

    if (PackReaderIsEmpty(reader, 1)) {
        return EIO;
    }

    if (PackGetType(reader) != E_PackTypeFloat) {
        return EINVAL;
    }

    fi.i = 0;
    type = reader->cursor[0];

    if (type == PACK_TYPE_FLOAT && !PackReaderIsEmpty(reader, 5)) {
        fi.i = (uint32_t)reader->cursor[1] << 24 | (uint32_t)reader->cursor[2] << 16 |
               (uint16_t)reader->cursor[3] << 8  | reader->cursor[4];
        reader->cursor += 5;
    }
    else {
        return EINVAL;
    }

    *value = fi.f;

    return 0;
}

int32_t PackReaderGetRawBytes (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    T_PackLen* length,              //!< Pointer to actual length.
    uint8_t* bytes,                 //!< Pointer to bytes.
    T_PackLen max_len               //!< Maximum length.
) {
    uint8_t type;
    T_PackLen len;

    if (PackReaderIsEmpty(reader, 1)) {
        return EIO;
    }

    if (PackGetType(reader) != E_PackTypeRawBytes) {
        return EINVAL;
    }

    len = 0;
    type = reader->cursor[0];

    if ((type & 0xE0) == PACK_TYPE_RAW_FIX)
    {
        len = (T_PackLen)(reader->cursor[0] & ~0xE0);
        if (len <= max_len && !PackReaderIsEmpty(reader, len + 1)) {
            memcpy(bytes, &reader->cursor[1], len);
        }
        else {
            return EIO;
        }
        reader->cursor += len + 1;
    }
    else
    if (type == PACK_TYPE_RAW_8 && !PackReaderIsEmpty(reader, 2)) {
        len = (T_PackLen)reader->cursor[1];
        if (len <= max_len && !PackReaderIsEmpty(reader, len + 2)) {
            memcpy(bytes, &reader->cursor[2], len);
        }
        else {
            return EIO;
        }
        reader->cursor += len + 2;
    }
    else

    if (type == PACK_TYPE_RAW_16 && !PackReaderIsEmpty(reader, 3)) {
        len = (T_PackLen)((uint16_t)reader->cursor[1] << 8 | reader->cursor[2]);
        if (len <= max_len && !PackReaderIsEmpty(reader, len + 3)) {
            memcpy(bytes, &reader->cursor[3], len);
        }
        else {
            return EIO;
        }
        reader->cursor += len + 3;
    }
    else
    if (type == PACK_TYPE_RAW_32 && !PackReaderIsEmpty(reader, 5)) {
        len = (uint32_t)reader->cursor[1] << 24 | (uint32_t)reader->cursor[2] << 16 |
              (uint16_t)reader->cursor[3] << 8  | reader->cursor[4];
        if (len <= max_len && !PackReaderIsEmpty(reader, len + 5)) {
            memcpy(bytes, &reader->cursor[5], len);
        }
        else {
            return EIO;
        }
        reader->cursor += len + 5;
    }
    else
    {
        return EIO;
    }

    *length = len;

    return 0;
}

int32_t PackReaderGetString (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    T_PackLen* length,              //!< Pointer to length.
    char* string,                   //!< Pointer to string.
    T_PackLen max_len               //!< Maximum length.
) {
    int32_t err;
    T_PackLen _length;

    err = PackReaderGetRawBytes(reader,&_length, (uint8_t*)string, max_len - 1);
    if (err != 0)
    {
        return err;
    }

    *(string + _length) = '\0';
    *length = _length;

    return 0;
}

void PackWriterSetBuffer (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    uint8_t* buffer,                //!< Pointer to the buffer to initialize.
    T_PackLen max_len               //!< Maximum length of the buffer.
) {
    writer->buffer_start = buffer;
    writer->buffer_len   = max_len;
    writer->cursor       = buffer;
}

static bool PackWriterIsFull (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    T_PackLen len                   //!< Data length.
) {
    if ((writer->cursor - writer->buffer_start + len) <= writer->buffer_len) {
        return false;
    }
    else {
        return true;
    }
}

int32_t PackWriterOpen (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    T_PackType type,                //!< Type.
    uint32_t nb_elem                //!< Number of elements.
) {
    if (type != E_PackTypeArray && type != E_PackTypeMap) {
        return EILSEQ;
    }

    if (nb_elem <= 0xF) {
        // Fix array or fix map
        if (!PackWriterIsFull(writer, 1)) {
            writer->cursor[0] = (type == E_PackTypeArray) ? PACK_TYPE_ARRAY_FIX : PACK_TYPE_MAP_FIX;
            writer->cursor[0] |= (uint8_t)(nb_elem);
            writer->cursor += 1;
        }
        else {
            return EIO;
        }
    }
    else if (nb_elem <= 0xFFFF) {
        // Array 16 or map 16
        if (!PackWriterIsFull(writer, 3)) {
            writer->cursor[0] = (type == E_PackTypeArray) ? PACK_TYPE_ARRAY_16 : PACK_TYPE_MAP_16;
            writer->cursor[1] = (uint8_t)((nb_elem >> 8) & 0xFF);
            writer->cursor[2] = (uint8_t)((nb_elem >> 0) & 0xFF);
            writer->cursor += 3;
        }
        else
        {
            return EIO;
        }
    }
    else {
        // Array 32 or map 32
        if (!PackWriterIsFull(writer, 5)) {
            writer->cursor[0] = (type == E_PackTypeArray) ? PACK_TYPE_ARRAY_32 : PACK_TYPE_MAP_32;
            writer->cursor[1] = (uint8_t)((nb_elem >> 24) & 0xFF);
            writer->cursor[2] = (uint8_t)((nb_elem >> 16) & 0xFF);
            writer->cursor[3] = (uint8_t)((nb_elem >> 8)  & 0xFF);
            writer->cursor[4] = (uint8_t)((nb_elem >> 0)  & 0xFF);
            writer->cursor += 5;
        }
        else {
            return EIO;
        }
    }

    return 0;
}

T_PackLen PackWriterGetOffset (
    struct S_PackWriter* writer     //!< Pointer to pack writer.
) {
    return (writer->cursor - writer->buffer_start);
}

int32_t PackWriterPutNil (
    struct S_PackWriter* writer     //!< Pointer to pack writer.
) {
    if (!PackWriterIsFull(writer, 1))
    {
        writer->cursor[0] = PACK_TYPE_NIL;
        writer->cursor +=1;
        return 0;
    }
    else {
        return EIO;
    }
}

int32_t PackWriterPutBoolean (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    bool value                      //!< Boolean value.
) {
    if (!PackWriterIsFull(writer, 1)) {
        writer->cursor[0] =
        value ? PACK_TYPE_BOOLEAN_TRUE : PACK_TYPE_BOOLEAN_FALSE;
        writer->cursor += 1;
        return 0;
    }
    else {
        return EIO;
    }
}

int32_t PackWriterPutSignedInteger (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    int32_t value                   //!< Signed integer.
) {
    if (value >= PACK_TYPE_NEGATIVE_FIXNUM_MIN && value <= PACK_TYPE_NEGATIVE_FIXNUM_MAX) {
        // Negative fixnum
        if (!PackWriterIsFull(writer, 1)) {
            writer->cursor[0] = PACK_TYPE_FIXNUM_NEGATIVE | (uint8_t)value;
            writer->cursor += 1;
        }
        else {
            return EIO;
        }
    }
    else
    if (value >= INT8_MIN && value <= INT8_MAX) {
        // Signed 8-bit integer
        if (!PackWriterIsFull(writer, 2)) {
            writer->cursor[0] = PACK_TYPE_INT8;
            writer->cursor[1] = (uint8_t)(value & 0xFF);
            writer->cursor += 2;
        }
        else {
            return EIO;
        }
    }
    else
    if (value >= INT16_MIN && value <= INT16_MAX) {
        if (!PackWriterIsFull(writer, 3)) {
            writer->cursor[0] = PACK_TYPE_INT16;
            writer->cursor[1] = (uint8_t)((value >> 8) & 0xFF);
            writer->cursor[2] = (uint8_t)((value >> 0) & 0xFF);
            writer->cursor += 3;
        }
        else
        {
            return EIO;
        }
    }
    else {
        // Signed 32-bit integer
        if (!PackWriterIsFull(writer, 5)) {
            writer->cursor[0] = PACK_TYPE_INT32;
            writer->cursor[1] = (uint8_t)((value >> 24) & 0xFF);
            writer->cursor[2] = (uint8_t)((value >> 16) & 0xFF);
            writer->cursor[3] = (uint8_t)((value >> 8)  & 0xFF);
            writer->cursor[4] = (uint8_t)((value >> 0)  & 0xFF);
            writer->cursor += 5;
        }
        else
        {
            return EIO;
        }
    }

    return 0;
}

int32_t PackWriterPutUnsignedInteger (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    uint32_t value                  //!< Unsigned integer.
) {
    if (value <= PACK_TYPE_POSITIVE_FIXNUM_MAX) {
        // Positive fixnum
        if (!PackWriterIsFull(writer, 1)) {
            writer->cursor[0] = PACK_TYPE_FIXNUM_POSITIVE | (uint8_t)value;
            writer->cursor += 1;
        }
        else {
            return EIO;
        }
    }
    else
    if (value <= UINT8_MAX) {
        // Unsigned 8-bit integer
        if (!PackWriterIsFull(writer, 2)) {
            writer->cursor[0] = PACK_TYPE_UINT8;
            writer->cursor[1] = (uint8_t)(value & 0xFF);
            writer->cursor += 2;
        }
        else {
            return EIO;
        }
    }
    else
    if (value <= UINT16_MAX) {
        // Unsigned 16-bit integer
        if (!PackWriterIsFull(writer, 3)) {
            writer->cursor[0] = PACK_TYPE_UINT16;
            writer->cursor[1] = (uint8_t)((value >> 8) & 0xFF);
            writer->cursor[2] = (uint8_t)((value >> 0) & 0xFF);
            writer->cursor += 3;
        }
        else {
            return EIO;
        }
    }
    else {
        // Unsigned 32-bit integer
        if (!PackWriterIsFull(writer, 5)) {
            writer->cursor[0] = PACK_TYPE_UINT32;
            writer->cursor[1] = (uint8_t)((value >> 24) & 0xFF);
            writer->cursor[2] = (uint8_t)((value >> 16) & 0xFF);
            writer->cursor[3] = (uint8_t)((value >> 8)  & 0xFF);
            writer->cursor[4] = (uint8_t)((value >> 0)  & 0xFF);
            writer->cursor += 5;
        }
        else {
            return EIO;
        }
    }

    return 0;
}

int32_t PackWriterPutFloat (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    T_PackFloat value               //!< Float value.
) {
    union {
        uint32_t i;
        T_PackFloat f;
    } fi;

    fi.f = value;

    // Unsigned 32-bit integer
    if (!PackWriterIsFull(writer, 5)) {
        writer->cursor[0] = PACK_TYPE_FLOAT;
        writer->cursor[1] = (uint8_t)((fi.i >> 24) & 0xFF);
        writer->cursor[2] = (uint8_t)((fi.i >> 16) & 0xFF);
        writer->cursor[3] = (uint8_t)((fi.i >> 8)  & 0xFF);
        writer->cursor[4] = (uint8_t)((fi.i >> 0)  & 0xFF);
        writer->cursor += 5;
    }
    else {
        return EIO;
    }

    return 0;
}

int32_t PackWriterPutRawBytes (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    uint8_t* raw,                   //!< Pointer to the first raw byte.
    uint32_t len                    //!< Number of raw bytes.
) {
    if (len <= 0x1F) {
        // Fix raw
        if (!PackWriterIsFull(writer, len + 1)) {
            writer->cursor[0] = PACK_TYPE_RAW_FIX | (uint8_t)len;
            memcpy(&writer->cursor[1], raw, len);
            writer->cursor += len + 1;
        }
        else {
            return EIO;
        }
    }
    else if (len <= 0xFF) {
        // Raw 8
        if (!PackWriterIsFull(writer, len + 3)) {
            writer->cursor[0] = PACK_TYPE_RAW_8;
            writer->cursor[1] = (uint8_t)len;
            memcpy(&writer->cursor[2], raw, len);
            writer->cursor += len + 2;
        }
        else {
            return EIO;
        }
    }
    else if (len <= 0xFFFF) {
        // Raw 16
        if (!PackWriterIsFull(writer, len + 3)) {
            writer->cursor[0] = PACK_TYPE_RAW_16;
            writer->cursor[1] = (uint8_t)((len >> 8 & 0xFF));
            writer->cursor[2] = (uint8_t)((len >> 0 & 0xFF));
            memcpy(&writer->cursor[3], raw, len);
            writer->cursor += len + 3;
        }
        else {
            return EIO;
        }
    }
    else {
        // Raw 32
        if (!PackWriterIsFull(writer, len + 5)) {
            writer->cursor[0] = PACK_TYPE_RAW_32;
            writer->cursor[1] = (uint8_t)((len >> 24 & 0xFF));
            writer->cursor[2] = (uint8_t)((len >> 16 & 0xFF));
            writer->cursor[3] = (uint8_t)((len >> 8  & 0xFF));
            writer->cursor[4] = (uint8_t)((len >> 0  & 0xFF));
            memcpy(&writer->cursor[5], raw, len);
            writer->cursor += len + 5;
        }
        else {
            return EIO;
        }
    }

    return 0;
}

int32_t PackWriterPutString (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    const char* str                 //!< Pointer to string.
) {
  return PackWriterPutRawBytes(writer, (uint8_t*)str, strlen(str));
}