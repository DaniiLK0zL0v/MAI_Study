#include <iostream>
#include <vector>
 
using namespace std;
 
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, k, m;
    if (cin >> n >> k >> m) {
        vector<bool> danger(n + 1, false);
        for (int i = 0; i < m; ++i) {
            int val;
            cin >> val;
            danger[val] = true;
        }
        
        vector<long long> dp(n + 1, 0);
        dp[0] = 1;
        long long sum = 0;
        const long long MOD = 1000000007LL;
        
        for (int i = 1; i <= n; ++i) {
            sum = (sum + dp[i - 1]) % MOD;
            if (i - 1 - k >= 0) {
                sum = (sum - dp[i - 1 - k] + MOD) % MOD;
            }
            
            if (danger[i]) {
                dp[i] = 0;
            } else {
                dp[i] = sum;
            }
        }
        
        cout << dp[n] << "\n";
    }
    return 0;
}
