// Copyright (C) 2007-2018 Ingo Korb <ingo@akana.de>, as NODISKEMU (a fork of sd2iec)
// carries it. GPL-2.0. Ported to C++ for PetMcp, 2026 (THIRD_PARTY.md).

// Ports NODISKEMU's src/buffers.c, and buffers.h's check_free_buffers() and
// get_dirty_buffer_count(), at commit e51a6df. Left out: the LEDs
// (set_busy_led, set_dirty_led, update_leds), which only showed what the
// counts below hold.

#include "sd2pet/Buffers.hpp"

#include "sd2pet/Dos.hpp"

#include <cstring>

namespace pet::sd2pet
{

/**
 * callback_dummy - dummy function for the buffer callbacks
 * @buf: pointer to a buffer
 *
 * This is the default function used for all buffer callbacks so
 * the caller doesn't need to check if the callbacks are valid.
 * It always returns 0 for success.
 */
std::uint8_t callbackDummy( Dos& /*dos*/, Buffer* /*buf*/ )
{
  return 0;
}

/**
 * buffers_init - initializes the buffer data structures
 *
 * This function initialized all the buffer-related data structures.
 */
void buffersInit( Dos& dos )
{
  BuffersState& b = dos.buffers;

  std::memset( static_cast<void*>( b.buffers.data() ), 0, sizeof( b.buffers ) );
  for ( std::size_t i = 0; i < BUFFER_COUNT; i++ )
    b.buffers.at( i ).data = b.bufferdata.data() + ( 256 * i );

  Buffer& error = b.buffers.at( ERRORBUFFER_IDX );
  error.data = dos.errors.errorBuffer.data();
  error.secondary = 15;
  error.allocated = true;
  error.read = true;
  error.write = true;
  error.sendeoi = true;
  error.refill = setOkMessage;
  error.cleanup = callbackDummy;
}

namespace
{

/**
 * alloc_specific_buffer - allocate a specific buffer for system use
 *
 * This function allocates the specified buffer and marks it as used.
 * Returns without doing anything if the buffer is already allocated.
 */
void allocSpecificBuffer( Dos& dos, std::size_t bufnum )
{
  Buffer& buf = dos.buffers.buffers.at( bufnum );
  if ( !buf.allocated )
  {
    /* Clear everything except the data pointer */
    std::uint8_t* const data = buf.data;
    std::memset( static_cast<void*>( &buf ), 0, sizeof( Buffer ) );
    buf.data = data;
    buf.allocated = true;
    buf.secondary = BUFFER_SEC_SYSTEM;
    buf.refill = callbackDummy;
    buf.cleanup = callbackDummy;
  }
}

} // namespace

/**
 * alloc_system_buffer - allocate a buffer for system use
 *
 * This function allocates a buffer and marks it as used. Returns a
 * pointer to the buffer structure or NULL of no buffer is free.
 */
Buffer* allocSystemBuffer( Dos& dos )
{
  for ( std::size_t i = 0; i < BUFFER_COUNT; i++ )
  {
    if ( !dos.buffers.buffers.at( i ).allocated )
    {
      allocSpecificBuffer( dos, i );
      return &dos.buffers.buffers.at( i );
    }
  }

  setError( dos, ERROR_NO_CHANNEL );
  return nullptr;
}

/**
 * alloc_buffer - allocates a buffer
 *
 * This function allocates a buffer and marks it as used. It will also
 * turn on the busy LED to notify the user. Returns a pointer to the
 * buffer structure or NULL if no buffer is free.
 */
Buffer* allocBuffer( Dos& dos )
{
  Buffer* buf = allocSystemBuffer( dos );
  if ( buf != nullptr )
  {
    buf->secondary = 0;
    dos.buffers.activeBuffers++;
  }
  return buf;
}

/**
 * alloc_linked_buffers - allocates linked buffers
 * @count    : Number of buffers to allocate
 *
 * This function allocates count buffers, marks them as used and
 * links them. It will also turn on the busy LED to notify the user.
 * Returns a pointer to the first buffer structure or NULL if
 * not enough buffers are free. The data segments of the allocated
 * buffers are guaranteed to be continuous.
 */
Buffer* allocLinkedBuffers( Dos& dos, std::uint8_t count )
{
  auto& buffers = dos.buffers.buffers;
  std::uint8_t freebufs = 0;
  std::size_t start = 0;

  for ( std::size_t i = 0; i < BUFFER_COUNT; i++ )
  {
    if ( buffers.at( i ).allocated )
    {
      /* Look for continuous buffers */
      /* Switching data segments is possible, but probably not required */
      freebufs = 0;
    }
    else
    {
      if ( freebufs == 0 )
        start = i;
      freebufs++;
      /* Found enough free space */
      if ( freebufs == count )
        break;
    }
  }

  if ( freebufs < count )
  {
    setError( dos, ERROR_NO_CHANNEL );
    return nullptr;
  }

  /* Chain the buffers */
  for ( std::size_t i = 0; i < count; i++ )
  {
    allocSpecificBuffer( dos, start + i );
    dos.buffers.activeBuffers++;
    buffers.at( start + i ).secondary = 0;
    // The last one's next may lie one past the buffers (the error channel's
    // for the last buffer); it is overwritten below, as in C.
    buffers.at( start + i ).pvt.buffer.next = &buffers.at( start + i + 1 );
    buffers.at( start + i ).pvt.buffer.first = &buffers.at( start );
    buffers.at( start + i ).pvt.buffer.size = count;
  }

  buffers.at( start + count - 1 ).pvt.buffer.next = nullptr;

  return &buffers.at( start );
}

/**
 * cleanup_and_free_buffer - cleanup and deallocate a buffer
 * @buffer: pointer to the buffer structure to cleanup and mark as free
 *
 * This function calls the cleanup function and deallocates the buffer.
 */
void cleanupAndFreeBuffer( Dos& dos, Buffer* buffer )
{
  buffer->cleanup( dos, buffer );
  freeBuffer( dos, buffer );
}

/**
 * free_buffer - deallocate a buffer
 * @buffer: pointer to the buffer structure to mark as free
 *
 * This function deallocates the given buffer. If the pointer is NULL,
 * the buffer is already freed or the buffer is assigned to secondary
 * address 15 nothing will happen. This function will also update the
 * two LEDs according to the remaining number of open and writable
 * buffers.
 */
void freeBuffer( Dos& dos, Buffer* buffer )
{
  if ( buffer == nullptr )
    return;
  if ( buffer->secondary == 15 )
    return;
  if ( !buffer->allocated )
    return;

  buffer->allocated = false;

  if ( buffer->dirty )
    dos.buffers.activeBuffers = static_cast<std::uint8_t>( dos.buffers.activeBuffers - 16 );
  if ( buffer->secondary < BUFFER_SEC_SYSTEM )
    dos.buffers.activeBuffers--;
}

/**
 * free_multiple_buffers - deallocates multiple buffers
 * @flags: Defines which buffers should be deallocated
 *
 * This function iterates over all buffers and frees those which match the
 * specification in flags (see FMB_* defines). If FMB_CLEAN is set it will
 * also call the cleanup function for those buffers which are about to be
 * freed. When FMB_CLEAN is set, the function returns 0 if all cleanup
 * functions returned 0 or 1 if at least one did not. When FMB_CLEAN is not
 * set, returns 0.
 */
std::uint8_t freeMultipleBuffers( Dos& dos, std::uint8_t flags )
{
  bool res = false;

  for ( std::size_t i = 0; i < BUFFER_COUNT; i++ )
  {
    Buffer& buf = dos.buffers.buffers.at( i );
    if ( buf.allocated && ( ( flags & FMB_FREE_SYSTEM ) != 0 || buf.secondary < BUFFER_SEC_SYSTEM ) &&
         ( ( flags & FMB_FREE_STICKY ) != 0 || !buf.sticky ) )
    {
      if ( ( flags & FMB_CLEAN ) != 0 )
      {
        // Short-circuits as C's ||: once one failed, no further cleanup is
        // called.
        res = res || buf.cleanup( dos, &buf ) != 0;
      }
      freeBuffer( dos, &buf );
    }
  }

  return res ? 1 : 0;
}

/**
 * find_buffer - find the buffer corresponding to a secondary address
 * @secondary: secondary address to look for
 *
 * This function returns a pointer to the first buffer structure whose
 * secondary address is the same as the one given. Returns NULL if
 * no matching buffer was found.
 */
Buffer* findBuffer( Dos& dos, std::uint8_t secondary )
{
  for ( Buffer& buf : dos.buffers.buffers )
  {
    if ( buf.allocated && buf.secondary == secondary )
      return &buf;
  }
  return nullptr;
}

/* Check if any buffers are free */
bool checkFreeBuffers( Dos const& dos )
{
  // As buffers.h's macro: with 32 buffers the low nibble is always below the
  // count, so this never fails.
  return ( dos.buffers.activeBuffers & 0x0fU ) < BUFFER_COUNT;
}

/* Return the number of dirty buffers */
std::uint8_t getDirtyBufferCount( Dos const& dos )
{
  return static_cast<std::uint8_t>( dos.buffers.activeBuffers >> 4U );
}

/**
 * mark_buffer_dirty - mark a buffer as dirty
 * @buf: pointer to the buffer
 *
 * This function marks the given buffer as dirty, tracks
 * this in active_buffers and turns on the dirty LED.
 */
void markBufferDirty( Dos& dos, Buffer* buf )
{
  if ( !buf->dirty )
  {
    buf->dirty = true;
    dos.buffers.activeBuffers = static_cast<std::uint8_t>( dos.buffers.activeBuffers + 16 );
  }
}

/**
 * mark_buffer_clean - mark a buffer as clean
 * @buf: pointer to the buffer
 *
 * This function marks the given buffer as clean, tracks
 * this in active_buffers and turns off the dirty LED if required.
 */
void markBufferClean( Dos& dos, Buffer* buf )
{
  if ( buf->dirty )
  {
    buf->dirty = false;
    dos.buffers.activeBuffers = static_cast<std::uint8_t>( dos.buffers.activeBuffers - 16 );
  }
}

} // namespace pet::sd2pet
