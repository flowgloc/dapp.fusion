#pragma once

int64_t fusion::safeAddInt64(const int64_t& a, const int64_t& b){
	const int64_t combinedValue = a + b;

	if(MAX_ASSET_AMOUNT - a < b){
		/** if the remainder is less than what we're adding, it means there 
		 *  will be overflow
		 */

		check(false, "overflow error");
	}	

	return combinedValue;
}

double fusion::safeDivDouble(const double& a, const double& b){
	if(b == 0){
		check( false, "cant divide double by 0" );
	}

	return a / b;
}

int64_t fusion::safeSubInt64(const int64_t& a, const int64_t& b){
	if(a == 0){
		check( b == 0, "subtraction would result in negative number" );
		return 0;
	}

	const int64_t remainder = a - b;

	if(b > a){
		check(false, "subtraction would result in negative number");
	}	

	return remainder;
}

