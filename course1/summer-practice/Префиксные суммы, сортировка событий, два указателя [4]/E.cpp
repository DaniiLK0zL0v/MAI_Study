#include <iostream>

using namespace std;

const long long MOD = 1000000007;

long long power(long long base, long long exp) {
    long long res = 1;
    base %= MOD;
    while (exp > 0) {
        if (exp % 2 == 1) {
            res = (res * base) % MOD;
        }
        base = (base * base) % MOD;
        exp /= 2;
    }
    return res;
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        long long sum = 0;
        for (int i = 0; i < n; ++i) {
            long long val;
            cin >> val;
            sum = (sum + val) % MOD;
        }
        sum = (sum + MOD) % MOD;
        
        long long ans = (sum * power(2, n - 1)) % MOD;
        cout << ans << "\n";
    }
    return 0;
}
