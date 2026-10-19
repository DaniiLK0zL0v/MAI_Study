#include <iostream>

using namespace std;

bool check(long long v, long long cnt1, long long cnt2, long long x, long long y) {
    long long div_x = v / x;
    long long div_y = v / y;
    long long div_xy = v / (x * y);
    
    if (v - div_x < cnt1) return false;
    if (v - div_y < cnt2) return false;
    if (v - div_xy < cnt1 + cnt2) return false;
    
    return true;
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    long long cnt1, cnt2, x, y;
    if (cin >> cnt1 >> cnt2 >> x >> y) {
        long long low = 1;
        long long high = 2000000000LL;
        long long ans = high;
        
        while (low <= high) {
            long long mid = low + (high - low) / 2;
            if (check(mid, cnt1, cnt2, x, y)) {
                ans = mid;
                high = mid - 1;
            } else {
                low = mid + 1;
            }
        }
        
        cout << ans << "\n";
    }
    return 0;
}
