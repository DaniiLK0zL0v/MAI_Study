#include <iostream>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        long long ans = 1;
        const long long MOD = 1000000007;
        
        for (int i = 3; i <= n - 1; ++i) {
            ans = (ans * i) % MOD;
        }
        
        cout << ans << "\n";
    }
    return 0;
}
