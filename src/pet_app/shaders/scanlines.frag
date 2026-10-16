#version 450
#extension GL_GOOGLE_include_directive : require
// Sharp pixels, each line of the source darkening towards its edges as a
// CRT's beam does: settings.x is how much.

#include "common.glsl"

void main()
{
  vec3 pixel = texture( source, sharpCoord( texCoord ) ).rgb;
  float fromCentre = fract( texCoord.y * sourceSize.y ) - 0.5;
  float beam = exp( -fromCentre * fromCentre * 12.0 );
  // Brightened by what the darkening takes away on average.
  float weight = mix( 1.0, beam * 1.35, settings.x );
  colour = vec4( min( pixel * weight, vec3( 1.0 ) ), 1.0 );
}
