#include <iostream>
#include "core/order.h"

using namespace std;

int main(){
    Order testorder = {
        "ord1",
        10.5,
        100,
        Instrument::Rose,
        Side::Buy
    };
    cout << "Client Order ID: " << testorder.clientOrderId << endl;
    return 0;
}