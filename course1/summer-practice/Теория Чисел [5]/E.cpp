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

long long modInverse(long long n) {
    return power(n, MOD - 2);
}

void solve() {
    long long b1, q, n;
    if (cin >> b1 >> q >> n) {
        if (n == 0) {
            cout << 0 << "\n";
            return;
        }
        if (q == 1) {
            cout << (b1 * n) % MOD << "\n";
            return;
        }
        
        long long term = (power(q, n) - 1 + MOD) % MOD;
        long long inv = modInverse((q - 1 + MOD) % MOD);
        long long ans = (b1 * term) % MOD;
        ans = (ans * inv) % MOD;
        cout << ans << "\n";
    }
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int t;
    if (cin >> t) {
        while (t--) {
            solve();
        }
    }
    return 0;
}
