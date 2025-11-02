cppcheck.exe --library=qt -q -DRELEASE --suppress=normalCheckLevelMaxBranches -DQT_VERSION_STR="123"  -ijson.hpp --check-level=exhaustive .
