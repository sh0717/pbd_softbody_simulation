#pragma once

// C++ 예외 비활성화 계약이 코어·바인딩·검사 대상에서 달라지면 컴파일을 중단합니다.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#error "PBD C++ targets must disable C++ exceptions."
#endif

#if defined(_MSC_VER) && (!defined(_HAS_EXCEPTIONS) || _HAS_EXCEPTIONS != 0)
#error "PBD MSVC targets must define _HAS_EXCEPTIONS=0."
#endif
