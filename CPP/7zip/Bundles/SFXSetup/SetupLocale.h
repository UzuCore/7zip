// Windows display language selects Korean; all other languages use English.
#ifndef Z7_SETUP_LOCALE_H
#define Z7_SETUP_LOCALE_H
inline bool IsKoreanSetup()
{
  return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_KOREAN;
}
inline const wchar_t *SetupText(const wchar_t *english, const wchar_t *korean)
{
  return IsKoreanSetup() ? korean : english;
}
#endif
