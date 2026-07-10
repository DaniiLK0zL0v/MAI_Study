#include <iostream>
#include <vector>
#include <string>

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

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    string s;
    if (cin >> s) {
        int l = s.length();
        vector<int> count(26, 0);
        for (char c : s) {
            count[c - 'a']++;
        }
        
        vector<long long> fact(l + 1, 1);
        for (int i = 2; i <= l; ++i) {
            fact[i] = (fact[i - 1] * i) % MOD;
        }
        
        long long num = fact[l];
        long long den = 1;
        for (int i = 0; i < 26; ++i) {
            if (count[i] > 1) {
                den = (den * fact[count[i]]) % MOD;
            }
        }
        
        long long ans = (num * modInverse(den)) % MOD;
        cout << ans << "\n";
    }
    return 0;
}
