// Copyright (C) 2007-2018 Ingo Korb <ingo@akana.de>, as NODISKEMU (a fork of sd2iec)
// carries it. GPL-2.0. Ported to C++ for PetMcp, 2026 (THIRD_PARTY.md).

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
