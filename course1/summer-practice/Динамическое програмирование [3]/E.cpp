#include <iostream>
#include <vector>
#include <algorithm>
 
using namespace std;
 
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, m;
    if (cin >> n >> m) {
        vector<vector<int>> gold(n, vector<int>(m));
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < m; ++j) {
                cin >> gold[i][j];
            }
        }
        
        vector<vector<int>> dp(n, vector<int>(m, 0));
        
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < m; ++j) {
                dp[i][j] = gold[i][j];
                int prev_max = -1;
                
                if (i > 0) {
                    prev_max = max(prev_max, dp[i - 1][j]);
                }
                if (j > 0) {
                    prev_max = max(prev_max, dp[i][j - 1]);
                }
                if (i > 0 && j > 0) {
                    prev_max = max(prev_max, dp[i - 1][j - 1]);
                }
                
                if (prev_max != -1) {
                    dp[i][j] += prev_max;
                }
            }
        }
        
        cout << dp[n - 1][m - 1] << "\n";
    }
    return 0;
}
