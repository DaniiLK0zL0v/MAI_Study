#include <iostream>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    long long n;
    int k;
    if (cin >> n >> k) {
        long long power_of_2 = 1LL << k;
        long long M = n / power_of_2;
        
        if (M == 0) {
            cout << -1 << "\n";
        } else {
            long long y = (M % 2 != 0) ? M : (M - 1);
            long long m = y * power_of_2;
            cout << m << "\n";
        }
    }
    return 0;
}