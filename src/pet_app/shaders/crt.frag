#version 450
#extension GL_GOOGLE_include_directive : require
// A CRT: the picture bent onto a curved tube (settings.y), its lines drawn by
// a beam (settings.x), through a mask of red, green and blue stripes
// (settings.z), and dimmed into the corners.

#include "common.glsl"

void main()
{
  // Barrel distortion about the centre.
  vec2 centred = texCoord * 2.0 - 1.0;
  float bend = settings.y * 0.12;
  centred *= 1.0 + bend * dot( centred, centred );
  vec2 coord = centred * 0.5 + 0.5;
  if ( any( lessThan( coord, vec2( 0.0 ) ) ) || any( greaterThan( coord, vec2( 1.0 ) ) ) )
  {
    colour = vec4( 0.0, 0.0, 0.0, 1.0 );
    return;
  }

  // Linear light for the blending, as a tube's phosphors add.
  vec3 pixel = pow( texture( source, sharpCoord( coord ) ).rgb, vec3( 2.2 ) );
  float fromCentre = fract( coord.y * sourceSize.y ) - 0.5;
  float beam = exp( -fromCentre * fromCentre * 14.0 );
  pixel *= mix( 1.0, beam * 1.45, settings.x );

  // The mask: a stripe of each colour, a target pixel wide.
  int stripe = int( mod( floor( texCoord.x * outputSize.x ), 3.0 ) );
  vec3 mask = vec3( stripe == 0 ? 1.0 : 0.0, stripe == 1 ? 1.0 : 0.0, stripe == 2 ? 1.0 : 0.0 );
  pixel *= mix( vec3( 1.0 ), mask * 2.6 + 0.2, settings.z * 0.45 );

  vec2 edge = coord * ( 1.0 - coord );
  float vignette = clamp( pow( edge.x * edge.y * 24.0, 0.18 ), 0.0, 1.0 );
  colour = vec4( pow( min( pixel * vignette, vec3( 1.0 ) ), vec3( 1.0 / 2.2 ) ), 1.0 );
}
