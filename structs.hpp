#pragma once

struct revenue_receiver {
	eosio::name  	beneficiary;
	double 			amount; //e.g. 0.1 = 10%
};