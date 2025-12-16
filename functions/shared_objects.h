class Body{
public:
  int x;
  Body(int val){
    ptr = std::make_unique<int>(val);
    x = *ptr;
  }
  ~Body(){
    std::cout << "Freeing Body ptr in destructor for pointer at " << ptr << std::endl;
  }
private:
  std::unique_ptr<int> ptr;
};
