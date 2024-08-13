@echo off
SET compiler=-nologo -Zi -W4 -wd4366 -wd4312 -wd4201 -wd4244 -wd4100 -wd4457 -wd4459 -wd4505 -wd4005 -wd4530 
SET linker=/IGNORE:4286 /IGNORE:4099 /IGNORE:4098 /OUT:Atlas" "Baked" "(windows).exe
SET definitions=/D _MBCS /D _CRT_SECURE_NO_WARNINGS
SET libraries=user32.lib gdi32.lib shell32.lib shcore.lib comdlg32.lib

cd ..
IF NOT EXIST a:/build mkdir build
cd build
cl %compiler% %definitions% a:/source/atlas_baked_windows.cpp %libraries% /link %linker%
cd ..
cd source



