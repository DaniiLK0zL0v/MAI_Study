#include <iostream>
#include <vector>
#include <algorithm>

using namespace std;

void solve() {
    int n;
    if (cin >> n) {
        vector<int> a(n);
        for (int i = 0; i < n; ++i) {
            cin >> a[i];
        }
        
        int swaps = 0;
        bool swapped = true;
        while (swapped) {
            swapped = false;
            for (int i = 0; i < n - 1; ++i) {
                if (a[i] > a[i + 1]) {
                    swap(a[i], a[i + 1]);
                    swaps++;
                    swapped = true;
                }
            }
        }
        cout << swaps << "\n";
    }
}

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int t;
    if (cin >> t) {
        while (t--) {
            solve();
        }
    }
    return 0;
}