#include <iostream>
#include <vector>

using namespace std;

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n, q;
    if (cin >> n >> q) {
        vector<long long> d(n + 2, 0);
        for (int i = 0; i < q; ++i) {
            int l, r;
            long long v;
            cin >> l >> r >> v;
            d[l] += v;
            d[r + 1] -= v;
        }
        
        long long current_val = 0;
        for (int i = 1; i <= n; ++i) {
            current_val += d[i];
            cout << current_val << (i == n ? "" : " ");
        }
        cout << "\n";
    }
    return 0;
}
