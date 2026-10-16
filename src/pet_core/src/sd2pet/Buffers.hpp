#pragma once

// buffers.c (NODISKEMU at commit e51a6df): the DOS's 256-byte buffers and
// the channels they serve.

#include "sd2pet/DosTypes.hpp"

#include <array>
#include <cstdint>

namespace pet::sd2pet
{

struct BuffersState
{
  /// Directory handle used in file matching
  Dh matchdh{};
  /// scratch space for use in fileops code
  std::array<std::uint8_t, 33> opsScratch{};
  /// The buffers; the last is the error channel's.
  std::array<Buffer, BUFFER_COUNT + 1> buffers{};
  std::array<std::uint8_t, BUFFER_COUNT * 256> bufferdata{};
  /// Number of currently allocated buffers + 16 * number of write buffers
  std::uint8_t activeBuffers{};
};

/* Initializes the buffer structures */
void buffersInit( Dos& dos );

/* Dummy callback */
std::uint8_t callbackDummy( Dos& dos, Buffer* buf );

/* Allocates a buffer for internal use */
Buffer* allocSystemBuffer( Dos& dos );

/* Allocates a buffer - returns pointer to buffer or NULL if failure */
Buffer* allocBuffer( Dos& dos );

/* Allocates linked buffers - returns pointer to first buffer or NULL if failure */
/* Buffers are guranteed to have continuous data segments. */
Buffer* allocLinkedBuffers( Dos& dos, std::uint8_t count );

/* Call the cleanup function and deallocate a buffer */
void cleanupAndFreeBuffer( Dos& dos, Buffer* buffer );

/* Deallocates a buffer */
void freeBuffer( Dos& dos, Buffer* buffer );

/* Deallocates multiple buffers */
std::uint8_t freeMultipleBuffers( Dos& dos, std::uint8_t flags );

/* Mark a buffer as sticky */
inline void stickBuffer( Buffer* buf )
{
  buf->sticky = true;
}

/* remove sticky mark */
inline void unstickBuffer( Buffer* buf )
{
  buf->sticky = false;
}

/* Finds the buffer corresponding to a secondary address */
/* Returns pointer to buffer on success or NULL on failure */
Buffer* findBuffer( Dos& dos, std::uint8_t secondary );

/* Check if any buffers are free */
bool checkFreeBuffers( Dos const& dos );

/* Return the number of dirty buffers */
std::uint8_t getDirtyBufferCount( Dos const& dos );

/* Mark a buffer as write-buffer and sticky it */
inline void markWriteBuffer( Buffer* buf )
{
  buf->write = true;
  stickBuffer( buf );
}

/* Mark a buffer as dirty */
void markBufferDirty( Dos& dos, Buffer* buf );

/* Mark a buffer as clean */
void markBufferClean( Dos& dos, Buffer* buf );

} // namespace pet::sd2pet
