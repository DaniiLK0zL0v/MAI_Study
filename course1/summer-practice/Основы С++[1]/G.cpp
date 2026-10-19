#include <iostream>
 
using namespace std;
 
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    long long n, k;
    if (cin >> n >> k) {
        long long low = 0, high = k;
        long long D = 0;
        
        while (low <= high) {
            long long mid = low + (high - low) / 2;
            if (mid == 0) {
                D = mid;
                low = mid + 1;
            } else if (2 * n + mid - 1 <= k / (2 * mid)) {
                D = mid;
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }
        
        long long s_D = 2 * D * (2 * n + D - 1);
        long long sum_D = 2 * n * D * (D + 1) + 4 * D * (D * D - 1) / 3;
        long long rem = k - s_D;
        
        long long total_sum = sum_D + rem * (D + 1);
        
        cout << total_sum << "\n";
    }
    return 0;
}