/******************************************************************************/
/*                                                                            */
/*  \file       msgpack.h                                                     */
/*  \date       Oct 2017                                                      */
/*  \author     Severin Leven, based on Julien Lefrique                       */
/*                                                                            */
/*  \brief      API of MessagePack object serialization library               */
/*                                                                            */
/******************************************************************************/
/*                                                                            */
/*  Target CPU: any 32bit                                                     */
/*  Ressources: CPU                                                           */
/*                                                                            */
/******************************************************************************/

#ifndef MSGPACK_H
#define MSGPACK_H


//----------//
// Includes //
//----------//

#include <stdint.h>     // for int data types
#include <stdbool.h>    // for bool data type


//-----------------//
// Public typedefs //
//-----------------//

typedef uint32_t T_PackLen;
typedef float    T_PackFloat;

typedef enum {
  E_PackTypeUnknown,
  E_PackTypeNil,
  E_PackTypeBoolean,
  E_PackTypeSignedInteger,
  E_PackTypeUnsignedInteger,
  E_PackTypeFloat,
  E_PackTypeRawBytes,
  E_PackTypeArray,
  E_PackTypeMap
} T_PackType;

struct S_PackReader {
  uint8_t*  buffer_start;
  T_PackLen buffer_len;
  uint8_t*  cursor;
};

struct S_PackWriter {
  uint8_t*  buffer_start;
  T_PackLen buffer_len;
  uint8_t*  cursor;
};


//-----//
// API //
//-----//

#ifdef __cplusplus
extern "C" {
#endif

/*!
 *  \brief  Returns the type of data.
 *  \return Type.
 */
T_PackType PackGetType (
    struct S_PackReader* reader     //!< Pointer to pack reader.
);

/*!
 *  \brief  Initializes reader with the buffer that contains serialized data.
 *  \return None.
 */
void PackReaderSetBuffer (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    uint8_t* buffer,                //!< Buffer containing serialized data.
    T_PackLen len                   //!< Length.
);

/*!
 *  \brief  Opens an array or a map.
 *  \return Error code.
 */
int32_t PackReaderOpen (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    T_PackType type,                //!< Type.
    T_PackLen* length               //!< Number of elements.
);

/*!
 *  \brief  Checks if the data is a Nil.
 *  \return Error code.
 */
int32_t PackReaderIsNil (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    bool*  is_nil                   //!< TRUE if a Nil, FALSE if not.
);

/*!
 *  \brief  Gets a boolean.
 *  \return Error code.
 */
int32_t PackReaderGetBoolean (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    bool*  is_true                  //!< Pointer to boolean.
);

/*!
 *  \brief  Gets a signed integer (one Byte wide).
 *  \return Error code.
 */
int32_t PackReaderGetSignedChar (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    int8_t* value                   //!< Pointer to signed char.
);

/*!
 *  \brief  Gets a signed integer (two Bytes wide).
 *  \return Error code.
 */
int32_t PackReaderGetSignedShort (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    int16_t* value                  //!< Pointer to signed short.
);

/*!
 *  \brief  Gets a signed integer (four Bytes wide).
 *  \return Error code.
 */
int32_t PackReaderGetSignedLong (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    int32_t* value                  //!< Pointer to signed long.
);

/*!
 *  \brief  Gets a signed integer (any Byte width).
 *  \return Error code.
 */
int32_t PackReaderGetSignedInteger (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    int32_t* value                  //!< Pointer to signed integer.
);

/*!
 *  \brief  Gets an unsigned integer (one Byte wide).
 *  \return Error code.
 */
int32_t PackReaderGetUnsignedChar (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    uint8_t* value                  //!< Pointer to unsigned char.
);

/*!
 *  \brief  Gets an unsigned integer (two Bytes wide).
 *  \return Error code.
 */
int32_t PackReaderGetUnsignedShort (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    uint16_t* value                 //!< Pointer to unsigned short.
);

/*!
 *  \brief  Gets an unsigned integer (four Bytes wide).
 *  \return Error code.
 */
int32_t PackReaderGetUnsignedLong (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    uint32_t* value                 //!< Pointer to unsigned long.
);

/*!
 *  \brief  Gets an unsigned integer (any Byte width).
 *  \return Error code.
 */
int32_t PackReaderGetUnsignedInteger (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    uint32_t* value                 //!< Pointer to unsigned integer.
);

/*!
 *  \brief  Gets a float.
 *  \return Error code.
 */
int32_t PackReaderGetFloat (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    T_PackFloat* value              //!< Pointer to float.
);

/*!
 *  \brief  Gets raw bytes.
 *  \return Error code.
 */
int32_t PackReaderGetRawBytes (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    T_PackLen* length,              //!< Pointer to actual length.
    uint8_t* bytes,                 //!< Pointer to bytes.
    T_PackLen max_len               //!< Maximum length.
);

/*!
 *  \brief  Gets a string.
 *  \return Error code.
 */
int32_t PackReaderGetString (
    struct S_PackReader* reader,    //!< Pointer to pack reader.
    T_PackLen* length,              //!< Pointer to length.
    char* string,                   //!< Pointer to string.
    T_PackLen max_len               //!< Maximum length.
);

/*!
 *  \brief  Initializes writer with the buffer ready to receive serialized data.
 *  \return None.
 */
void PackWriterSetBuffer (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    uint8_t* buffer,                //!< Pointer to the buffer to initialize.
    T_PackLen max_len               //!< Maximum length of the buffer.
);

/*!
 *  \brief  Opens an array or a map.
 *  \return Error code.
 */
int32_t PackWriterOpen (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    T_PackType type,                //!< Type.
    uint32_t nb_elem                //!< Number of elements.
);

/*!
 *  \brief  Gets the number of bytes already packed.
 *  \return Number of bytes already packed.
 */
T_PackLen PackWriterGetOffset (
    struct S_PackWriter* writer     //!< Pointer to pack writer.
);

/*!
 *  \brief  Puts a Nil.
 *  \return Error code.
 */
int32_t PackWriterPutNil (
    struct S_PackWriter* writer     //!< Pointer to pack writer.
);

/*!
 *  \brief  Puts a boolean.
 *  \return Error code.
 */
int32_t PackWriterPutBoolean (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    bool value                      //!< Boolean value.
);

/*!
 *  \brief  Puts a signed integer.
 *  \return Error code.
 */
int32_t PackWriterPutSignedInteger (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    int32_t value                   //!< Signed integer.
);

/*!
 *  \brief  Puts an unsigned integer.
 *  \return Error code.
 */
int32_t PackWriterPutUnsignedInteger (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    uint32_t value                  //!< Unsigned integer.
);

/*!
 *  \brief  Puts a float.
 *  \return Error code.
 */
int32_t PackWriterPutFloat (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    T_PackFloat value               //!< Float value.
);

/*!
 *  \brief  Puts raw bytes.
 *  \return Error code.
 */
int32_t PackWriterPutRawBytes (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    uint8_t* raw,                   //!< Pointer to the first raw byte.
    uint32_t len                    //!< Number of raw bytes.
);

/*!
 *  \brief  Puts a string.
 *  \return Error code.
 */
int32_t PackWriterPutString (
    struct S_PackWriter* writer,    //!< Pointer to pack writer.
    const char* str                 //!< Pointer to string.
);

#ifdef __cplusplus
}
#endif

#endif /* MSGPACK_H */
