#include <iostream>
#include <vector>
#include <algorithm>
 
using namespace std;
 
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        vector<int> c(n);
        for (int i = 0; i < n; ++i) {
            cin >> c[i];
        }
        
        sort(c.begin(), c.end());
        
        bool ok = true;
        for (int i = 0; i < n; ++i) {
            if (c[i] != c[0] + i) {
                ok = false;
                break;
            }
        }
        
        if (ok) {
            cout << "Deck looks good\n";
        } else {
            cout << "Scammed\n";
        }
    }
    return 0;
}
