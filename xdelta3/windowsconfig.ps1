$content = Get-Content -Path ".\cmake-config.h.in"
$content = $content -replace '@SIZEOF_SIZE_T@', '8'
$content = $content -replace '@SIZEOF_UNSIGNED_INT@', '4'
$content = $content -replace '@SIZEOF_UNSIGNED_LONG@', '8'
$content = $content -replace '@SIZEOF_UNSIGNED_LONG_LONG@', '8'
$content = $content -replace '#cmakedefine HAVE_LZMA_H @HAVE_LZMA_H@', '/* #undef HAVE_LZMA_H */'
$content = $content -replace '#cmakedefine HAVE_LIBLZMA @HAVE_LIBLZMA@', '/* #undef HAVE_LIBLZMA */'
$content = $content -replace '#cmakedefine', '#define'
Set-Content -Path ".\config.h" -Value $content