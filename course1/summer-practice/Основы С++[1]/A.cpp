#include <iostream>
#include <vector>
#include <algorithm>
 
using namespace std;
 
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        vector<int> a(n);
        for (int i = 0; i < n; ++i) {
            cin >> a[i];
        }
        
        int l, r;
        while (cin >> l >> r && (l != 0 || r != 0)) {
            reverse(a.begin() + l - 1, a.begin() + r);
        }
        
        for (int i = 0; i < n; ++i) {
            cout << a[i] << (i == n - 1 ? "" : " ");
        }
        cout << "\n";
    }
    return 0;
}