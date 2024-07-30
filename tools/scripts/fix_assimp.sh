#!/bin/sh

set -e
if [ ! -d code ]; then
	echo This script must be run in rts/lib/assimp!
	exit 1
fi

replace_func () {
	sed -i "s/\([^:a-zA-Z0-9_]\|^\)std::\(fabs\|sin\|cos\|cos\|sinh\|cosh\|tan\|tanh\|asin\|acos\|atan\|atan2\|ceil\|floor\|fmod\|hypot\|pow\|log\|log10\|exp\|frexp\|ldexp\|isnan\|isinf\|isfinite\|sqrt\|isqrt\)\(f\|l\)\?\b/\1math::\2/g" $file
	#sed -i "s/\([^:a-zA-Z0-9_]\|^\)\(fabs\|sin\|cos\|cos\|sinh\|cosh\|tan\|tanh\|asin\|acos\|atan\|atan2\|ceil\|floor\|fmod\|hypot\|pow\|log\|log10\|exp\|frexp\|ldexp\|isnan\|isinf\|isfinite\|sqrt\|isqrt\)\(f\|l\)\? *(/\1math::\2(/g" $file
}

#echo "Assimp vanilla version tag: `git describe --exact-match --tags`" >  AssimpRecoilVersion.txt
#echo "Assimp vanilla  commit  id: `git rev-parse HEAD`"                >> AssimpRecoilVersion.txt
#echo "Modified after runnning ../../../tools/scripts/fix_assimp.sh"    >> AssimpRecoilVersion.txt

for file in $(find -name '*.cpp' -or -name '*.h' -or -name '*.inl');
do
	sed -i 's/<cmath>/"lib\/streflop\/streflop_cond.h"/g' $file
	sed -i 's/<math.h>/"lib\/streflop\/streflop_cond.h"/g' $file
	sed -i 's/"math.h"/"lib\/streflop\/streflop_cond.h"/g' $file
	sed -i 's/std::sort/std::stable_sort/g' $file
	#sed -i 's/double/float/g' $file
	replace_func
	echo Processed $file
done

# Handle https://github.com/beyond-all-reason/spring/commit/a2f88fe97f604cea4991a36cd995f527852808cb
FA=`find -iname "fast_atof.h"`
sed -i "s/ double fast_atof_table/ float fast_atof_table/g" $FA
sed -i "s/0.0,/0.0f,/g" $FA
sed -i "s/01/01f/g" $FA
sed -i "s/0.1,/0.1f,/g" $FA
echo Processed $FA

FA=`find -iname "ScenePreprocessor.cpp"`
sed -i "s/std::min(first, 0.);/std::min(first, static_cast<decltype(first)>(0));/g" $FA
echo Processed $FA

#sed -i 's/double/float/g' code/PolyTools.h