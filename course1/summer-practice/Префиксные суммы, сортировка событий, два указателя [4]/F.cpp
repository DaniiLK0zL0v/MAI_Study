#include <iostream>
#include <vector>
#include <algorithm>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    long long k;
    if (cin >> n >> k) {
        vector<long long> a(n);
        for (int i = 0; i < n; ++i) {
            cin >> a[i];
        }
        
        sort(a.begin(), a.end());
        
        long long ans = 0;
        int L = 0;
        for (int R = 0; R < n; ++R) {
            while (a[R] - a[L] > k) {
                L++;
            }
            long long len = R - L;
            if (len >= 2) {
                ans += len * (len - 1) / 2;
            }
        }
        
        cout << ans << "\n";
    }
    return 0;
}
