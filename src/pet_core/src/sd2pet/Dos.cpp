#include "sd2pet/Dos.hpp"

namespace pet::sd2pet
{

void dosInit( Dos& dos )
{
  buffersInit( dos );
  fatopsInit( dos, 0 );
  setError( dos, ERROR_DOSVERSION );
}

} // namespace pet::sd2pet
