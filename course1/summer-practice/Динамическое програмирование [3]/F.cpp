#include <iostream>
#include <vector>
#include <algorithm>
 
using namespace std;
 
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        vector<long long> dp(n + 1, 0);
        dp[1] = 0;
        for (int i = 2; i <= n; ++i) {
            long long best = dp[i - 1];
            if ((i & 1) == 0) {
                best = min(best, dp[i >> 1]);
            }
            if (i % 3 == 0) {
                best = min(best, dp[i / 3]);
            }
            dp[i] = best + i;
        }
        cout << dp[n] << "\n";
    }
    return 0;
}
