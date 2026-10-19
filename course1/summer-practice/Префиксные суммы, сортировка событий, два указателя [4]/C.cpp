#include <iostream>
#include <vector>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        vector<long long> price(n + 2, 0);
        for (int i = 1; i <= n; ++i) {
            long long a;
            cin >> a;
            price[i + 1] = price[i] + a;
        }
        
        long long max_profit = 0;
        int best_buy = -1;
        int best_sell = -1;
        int min_idx = 1;
        
        for (int sell = 2; sell <= n + 1; ++sell) {
            long long profit = price[sell] - price[min_idx];
            if (profit > max_profit) {
                max_profit = profit;
                best_buy = min_idx;
                best_sell = sell;
            }
            
            if (price[sell] < price[min_idx]) {
                min_idx = sell;
            }
        }
        
        if (max_profit > 0) {
            cout << best_buy << " " << best_sell << "\n";
        } else {
            cout << "-1 -1\n";
        }
    }
    return 0;
}
