#include "Video.hpp"

#include "pet/video/Screen.hpp"

namespace pet::machine
{

// The picture is the text area, a dot of it a dot of the picture.
static_assert( video::SCREEN_WIDTH == std::size_t{ TEXT_COLUMNS } * 8 && video::SCREEN_HEIGHT == TEXT_LINES );

Video::Video( std::span<std::uint8_t const> characters, std::span<std::uint8_t const> videoRam )
    : mCharacters{ characters }, mVideoRam{ videoRam }, mDots( video::SCREEN_WIDTH * video::SCREEN_HEIGHT, 0 ),
      mPicture( video::SCREEN_WIDTH * video::SCREEN_HEIGHT * 4, 0 )
{
  for ( std::size_t alpha = 3; alpha < mPicture.size(); alpha += 4 )
  {
    mPicture[alpha] = 0xff;
  }
}

Video::Event Video::cycle( Clock now, bool textSet )
{
  Clock const position = inFrame( now );
  auto const line = static_cast<unsigned>( position / CYCLES_PER_LINE );
  auto const column = static_cast<unsigned>( position % CYCLES_PER_LINE );

  // crtc.c's raster alarm at the end of line 259 and of line 199; the first
  // frame begins with the display off.
  if ( position == 0 && now > CYCLES_PER_FRAME )
  {
    mDisplayEnabled = true;
  }
  else if ( position == TEXT_LINES * CYCLES_PER_LINE )
  {
    mDisplayEnabled = false;
  }

  if ( mDisplayEnabled && line < TEXT_LINES && column < TEXT_COLUMNS )
  {
    std::uint8_t const code = mVideoRam[( ( ( line / 8 ) * TEXT_COLUMNS ) + column ) & 0x3ffU];
    std::size_t const glyph = ( mTextSet ? 0x400U : 0U ) + ( ( code & 0x7fU ) * 8U ) + ( line & 7U );
    std::uint8_t row = mCharacters[glyph];
    if ( ( code & 0x80U ) != 0 )
    {
      row = static_cast<std::uint8_t>( ~row );
    }
    std::uint8_t* const dots = &mDots[( std::size_t{ line } * video::SCREEN_WIDTH ) + ( std::size_t{ column } * 8 )];
    for ( unsigned bit = 0; bit < 8; ++bit )
    {
      dots[bit] = static_cast<std::uint8_t>( ( row >> ( 7U - bit ) ) & 1U );
    }
  }
  // The set chosen in this cycle's access reaches the next glyph.
  mTextSet = textSet;

  // The adjusted retrace alarm: off screen is what the display enable is at
  // that moment, which is on at the blank's start and off at its end.
  if ( position == VBLANK_START )
  {
    mOffScreen = mDisplayEnabled;
    for ( std::size_t dot = 0; dot < mDots.size(); ++dot )
    {
      std::uint8_t const level = mDots[dot] != 0 ? 0xff : 0x00;
      mPicture[dot * 4] = level;
      mPicture[( dot * 4 ) + 1] = level;
      mPicture[( dot * 4 ) + 2] = level;
    }
    ++mPicturesDrawn;
    return mOffScreen ? Event::BLANK_BEGINS : Event::BLANK_ENDS;
  }
  if ( position == VBLANK_END )
  {
    mOffScreen = mDisplayEnabled;
    return mOffScreen ? Event::BLANK_BEGINS : Event::BLANK_ENDS;
  }
  return Event::NONE;
}

namespace
{

/// The dots of `bytes`, every `stride`th one lit when it is not 0, a bit a dot.
std::vector<std::uint8_t> pack( std::span<std::uint8_t const> bytes, std::size_t stride )
{
  std::vector<std::uint8_t> bits( ( ( bytes.size() / stride ) + 7 ) / 8 );
  for ( std::size_t dot = 0; dot * stride < bytes.size(); ++dot )
  {
    if ( bytes[dot * stride] != 0 )
    {
      bits[dot / 8] |= static_cast<std::uint8_t>( 1U << ( dot % 8 ) );
    }
  }
  return bits;
}

bool litIn( std::vector<std::uint8_t> const& bits, std::size_t dot )
{
  return ( bits[dot / 8] & ( 1U << ( dot % 8 ) ) ) != 0;
}

} // namespace

std::vector<std::uint8_t> Video::packDots() const
{
  return pack( mDots, 1 );
}

std::vector<std::uint8_t> Video::packPicture() const
{
  return pack( mPicture, 4 );
}

void Video::unpack( std::vector<std::uint8_t> const& drawing, std::vector<std::uint8_t> const& complete )
{
  std::size_t const bytes = ( mDots.size() + 7 ) / 8;
  if ( drawing.size() != bytes || complete.size() != bytes )
  {
    return;
  }
  for ( std::size_t dot = 0; dot < mDots.size(); ++dot )
  {
    mDots[dot] = litIn( drawing, dot ) ? 1 : 0;
    std::uint8_t const level = litIn( complete, dot ) ? 0xff : 0x00;
    mPicture[dot * 4] = level;
    mPicture[( dot * 4 ) + 1] = level;
    mPicture[( dot * 4 ) + 2] = level;
  }
}

bool Video::offScreen() const
{
  return mOffScreen;
}

std::span<std::uint8_t const> Video::picture() const
{
  return mPicture;
}

std::int64_t Video::picturesDrawn() const
{
  return mPicturesDrawn;
}

} // namespace pet::machine
