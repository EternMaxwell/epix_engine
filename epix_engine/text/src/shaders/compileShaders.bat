
@echo off
pushd "%~dp0"

for %%f in (*.vert) do (
	echo Compiling %%f
	glslangValidator -V "%%f" -o "%%~nf.vert.h" --vn "%%~nf_vert"
	if exist "%%~nf.frag" (
		echo Compiling %%~nf.frag
		glslangValidator -V "%%~nf.frag" -o "%%~nf.frag.h" --vn "%%~nf_frag"
	)
)

popd

pause >nul
