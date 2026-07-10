#include <iostream>
#include <queue>
#include <vector>
 
using namespace std;
 
int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);
    
    int n;
    if (cin >> n) {
        priority_queue<int> left;
        priority_queue<int, vector<int>, greater<int>> right;
        
        for (int i = 0; i < n; ++i) {
            int x;
            cin >> x;
            
            if (left.empty() || x <= left.top()) {
                left.push(x);
            } else {
                right.push(x);
            }
            
            if (left.size() > right.size() + 1) {
                right.push(left.top());
                left.pop();
            } else if (left.size() < right.size()) {
                left.push(right.top());
                right.pop();
            }
            
            cout << left.top() << "\n";
        }
    }
    return 0;
}
