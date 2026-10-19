#include <iostream>
#include <vector>
 
using namespace std;
 
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int l, r;
    if (cin >> l >> r) {
        const int MOD = 1000000007;
        vector<int> dp(r + 1, 0);
        dp[1] = 1;
        
        for (int i = 1; i <= r; ++i) {
            int val = dp[i];
            for (int j = 2 * i; j <= r; j += i) {
                dp[j] += val;
                if (dp[j] >= MOD) {
                    dp[j] -= MOD;
                }
            }
        }
        
        int ans = 0;
        for (int i = l; i <= r; ++i) {
            ans += dp[i];
            if (ans >= MOD) {
                ans -= MOD;
            }
        }
        
        cout << ans << "\n";
    }
    return 0;
}
