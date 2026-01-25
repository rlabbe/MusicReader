@echo off
for %%f in (*.cpp *.h) do clang-format -i "%%f"
