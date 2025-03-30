@echo off
REM Upewnij się, że plik mkspiffs.exe znajduje się w tym samym folderze co ten skrypt.
REM Uruchamia mkspiffs.exe z parametrami:
REM -c .\dist   – folder z plikami do spakowania
REM -b 4096     – rozmiar bloku
REM -p 256      – rozmiar strony
REM -s 0x1F8000 – rozmiar obrazu (w tym przypadku 0x1F8000 bajtów)
REM www.bin    – nazwa pliku wynikowego

.\mkspiffs.exe -c .\dist -b 4096 -p 256 -s 0x1F8000 www.bin