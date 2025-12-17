class Body{
public:
  int x;
  Body(int val){
    x = val;
  }
  ~Body(){
    std::cout << "Freeing Body in destructor" << std::endl;
  }
};
