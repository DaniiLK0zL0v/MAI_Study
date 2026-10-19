#include <iostream>
#include <vector>
 
using namespace std;
 
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, m, k;
    if (cin >> n >> m >> k) {
        vector<vector<bool>> danger(n + 1, vector<bool>(m + 1, false));
        for (int i = 0; i < k; ++i) {
            int x, y;
            cin >> x >> y;
            danger[x][y] = true;
        }
        
        const int MOD = 1000000007;
        vector<vector<int>> dp(n + 1, vector<int>(m + 1, 0));
        
        dp[0][0] = 1;
        
        for (int i = 0; i <= n; ++i) {
            for (int j = 0; j <= m; ++j) {
                if (i == 0 && j == 0) continue;
                
                if (danger[i][j]) {
                    dp[i][j] = 0;
                } else {
                    int val = 0;
                    if (i > 0) {
                        val += dp[i - 1][j];
                    }
                    if (j > 0) {
                        val += dp[i][j - 1];
                    }
                    if (val >= MOD) {
                        val -= MOD;
                    }
                    dp[i][j] = val;
                }
            }
        }
        
        cout << dp[n][m] << "\n";
    }
    return 0;
}
