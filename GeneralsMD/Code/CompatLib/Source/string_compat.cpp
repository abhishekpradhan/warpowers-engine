#include "string_compat.h"

#include <sstream>
#include <streambuf>
#include <ostream>

// Igroteka @bugfix 10/07/2026 The stringbuf version silently wrote NOTHING:
// libc++'s basic_stringbuf::setbuf is a no-op (allowed by the standard), so
// the digits went to the stringbuf's internal string and `str` was returned
// as uninitialized stack garbage — rendered as a tofu box everywhere itoa
// feeds UI text (Disconnection Menu vote counts / countdowns).
char* itoa(int value, char* str, int base)
{
  static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
  if (base < 2 || base > 36) { str[0] = '\0'; return str; }

  char tmp[33];
  int pos = 0;
  // MSVC semantics: only base 10 is signed; other bases format the bit pattern.
  unsigned int uvalue;
  bool negative = (base == 10 && value < 0);
  if (negative)
    uvalue = (unsigned int)(-(long long)value);
  else
    uvalue = (unsigned int)value;

  do {
    tmp[pos++] = digits[uvalue % (unsigned int)base];
    uvalue /= (unsigned int)base;
  } while (uvalue != 0);

  int out = 0;
  if (negative) str[out++] = '-';
  while (pos > 0) str[out++] = tmp[--pos];
  str[out] = '\0';
  return str;
}

int _vsnwprintf(wchar_t* buffer, size_t count, const wchar_t* format, va_list args)
{
  std::wstring format_fixup(format);

  // Igroteka @bugfix 10/07/2026 MSVC '%hs' (narrow string in a wide format)
  // doesn't exist in musl's vswprintf — rewrite to plain '%s' (POSIX wide
  // printf's %s already means narrow) BEFORE the %s->%ls pass below would
  // otherwise leave it as the unsupported '%hs'.
  size_t pos = format_fixup.find(L"%hs", 0);
  while (pos != std::wstring::npos)
  {
    format_fixup.replace(pos, 3, L"%\x01s"); // placeholder so %s pass skips it
    pos = format_fixup.find(L"%hs", pos);
  }

  // Replace all %s with %ls
  pos = format_fixup.find(L"%s", 0);
  while (pos != std::wstring::npos)
  {
    format_fixup.replace(pos, 2, L"%ls");
    pos += 3;
    pos = format_fixup.find(L"%s", pos);
  }

  // Replace all %S with %s
  pos = format_fixup.find(L"%S", 0);
  while (pos != std::wstring::npos)
  {
    format_fixup.replace(pos, 2, L"%s");
    pos += 2;
    pos = format_fixup.find(L"%S", pos);
  }

  // resolve the %hs placeholders to plain narrow %s
  pos = format_fixup.find(L"%\x01s", 0);
  while (pos != std::wstring::npos)
  {
    format_fixup.replace(pos, 3, L"%s");
    pos = format_fixup.find(L"%\x01s", pos);
  }

  return vswprintf(buffer, count, format_fixup.c_str(), args);
}

// Also defined in GameSpy gsplatformutil
__attribute__((weak))
char* _strlwr(char* str)
{
  for (int i = 0; str[i] != '\0'; i++)
  {
    str[i] = tolower(str[i]);
  }
  return str;
}

// GeneralsX @build fbraz 11/02/2026 BenderAI - Linux portability: uppercase string
__attribute__((weak))
char* _strupr(char* str)
{
  for (int i = 0; str[i] != '\0'; i++)
  {
    str[i] = toupper(str[i]);
  }
  return str;
}