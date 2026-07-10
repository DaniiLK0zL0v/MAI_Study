#include <iostream>
 
using namespace std;
 
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    long long a0;
    if (cin >> n >> a0) {
        long long cur = a0;
        long long max1 = -1;
        long long max2 = -1;
        
        const long long MOD = 1000000007LL;
        
        for (int i = 1; i <= n; ++i) {
            cur = (cur * 5) % MOD;
            if (cur > max1) {
                max2 = max1;
                max1 = cur;
            } else if (cur > max2) {
                max2 = cur;
            }
        }
        
        cout << max2 << " " << max1 << "\n";
    }
    return 0;
}
