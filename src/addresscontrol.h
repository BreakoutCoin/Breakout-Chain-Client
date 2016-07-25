#ifndef ADDRESSCONTROL_H
#define ADDRESSCONTROL_H

#include <set>

typedef std::pair<std::string, std::string> AddrPair;

/** Address Control Features. */
class CAddressControl
{
public:

    CAddressControl()
    {
        SetNull();
    }
        
    void SetNull()
    {
        setSelected.clear();
    }
    
    bool HasSelected() const
    {
        return (setSelected.size() > 0);
    }
    
    bool IsSelected(AddrPair &addr)
    {
        return (setSelected.count(addr) > 0);
    }
    
    void Select(AddrPair &addr)
    {
        setSelected.insert(addr);
    }
    
    void UnSelect(AddrPair &addr)
    {
        setSelected.erase(addr);
    }
    
    void UnSelectAll()
    {
        setSelected.clear();
    }

    void ListSelected(std::vector<AddrPair>& vAddrs)
    {
        vAddrs.assign(setSelected.begin(), setSelected.end());
    }

    bool GetSelected(AddrPair &addr)
    {
         if (HasSelected())
	 {
                // get first element as only one should ever be selected
	        addr = *setSelected.begin();
	 }
	 else
	 {
	        return false;
	 }
         return true;
    }
private:
    std::set<AddrPair> setSelected;

};

#endif // ADDRESSCONTROL_H
